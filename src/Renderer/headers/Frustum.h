#pragma once

#include <glm/glm.hpp>
#include <cmath>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86_FP)
    #include <immintrin.h>
    #define FRUSTUM_USE_SIMD 1
#endif

/*
 * Objects behind the camera are not drawn to save processing time. The mathematical
 * planes are extracted and checked using fast instructions if the computer supports them.
 * Reference: http://gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
 */
class Frustum {
public:
    /**
     * @brief Extraction of 6 planes from the world camera matrices.
     * 
     * The data is stored in a way that allows the computer to check multiple planes 
     * at the same time.
     * @param vp The combined View-Projection matrix.
     */
    void update(const glm::mat4 &vp) {
        // Extract planes using Gribb/Hartmann method
        // Left, Right, Bottom, Top, Near, Far

        // Direct extraction and inline normalization are performed for simplicity.
        auto extractAndNormalize = [&vp](int col, float sign, int) {
            const float x = vp[0][3] + sign * vp[0][col];
            const float y = vp[1][3] + sign * vp[1][col];
            const float z = vp[2][3] + sign * vp[2][col];
            const float w = vp[3][3] + sign * vp[3][col];

            const float invLen = 1.0f / std::sqrt(x*x + y*y + z*z);
            return glm::vec4(x * invLen, y * invLen, z * invLen, w * invLen);
        };

        const glm::vec4 planes[6] = {
            extractAndNormalize(0,  1.0f, 0),  // Left
            extractAndNormalize(0, -1.0f, 1),  // Right
            extractAndNormalize(1,  1.0f, 2),  // Bottom
            extractAndNormalize(1, -1.0f, 3),  // Top
            extractAndNormalize(2,  1.0f, 4),  // Near
            extractAndNormalize(2, -1.0f, 5)   // Far
        };

        // Store the data in groups for efficiency
        for (int i = 0; i < 6; ++i) {
            planesX[i] = planes[i].x;
            planesY[i] = planes[i].y;
            planesZ[i] = planes[i].z;
            planesW[i] = planes[i].w;
        }

        // The array is padded with duplicates of the last plane to reach 8 entries.
        // This allows for processing in groups of 4 without extra logic.
        planesX[6] = planesX[7] = planes[5].x;
        planesY[6] = planesY[7] = planes[5].y;
        planesZ[6] = planesZ[7] = planes[5].z;
        planesW[6] = planesW[7] = planes[5].w;
    }

    /**
     * @brief This function checks if a box is inside the camera's view.
     * 
     * Efficiency considerations:
     * - **Fast Calculations**: Multiple checks are done at once to save time.
     * - **Data Layout**: Organized so the computer can load the data quickly.
     * - **Optimized Math**: Uses faster hardware features if available.
     * - **Clean Logic**: Minimized extra checks in the code that runs every frame.
     * @param center The center of the AABB in world space.
     * @param extents The half-extents (dimensions divided by 2) of the AABB.
     * @return true if the box is visible (inside or intersecting), false if culled.
     */
    [[nodiscard]] bool isBoxVisible(const glm::vec3 &center, const glm::vec3 &extents) const {
#ifdef FRUSTUM_USE_SIMD
        // Load center and extents into SIMD registers
        const __m128 cx = _mm_set1_ps(center.x);
        const __m128 cy = _mm_set1_ps(center.y);
        const __m128 cz = _mm_set1_ps(center.z);
        const __m128 ex = _mm_set1_ps(extents.x);
        const __m128 ey = _mm_set1_ps(extents.y);
        const __m128 ez = _mm_set1_ps(extents.z);

        // A mask to get the absolute value quickly.
        const __m128 signMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));

        // Accumulator for rejection test (OR all comparisons)
        __m128 anyReject = _mm_setzero_ps();

        // All 8 planes (including the 2 extra ones) are checked in two batches of 4.
        // This approach simplifies the loop structure.
        for (int i = 0; i < 8; i += 4) {
            // Load 4 planes at once
            const __m128 px = _mm_load_ps(&planesX[i]);
            const __m128 py = _mm_load_ps(&planesY[i]);
            const __m128 pz = _mm_load_ps(&planesZ[i]);
            const __m128 pw = _mm_load_ps(&planesW[i]);

            // Compute radius: r = dot(extents, abs(plane.xyz))
            // This is the maximum distance the box can extend from center towards the plane
            const __m128 abs_px = _mm_and_ps(px, signMask);
            const __m128 abs_py = _mm_and_ps(py, signMask);
            const __m128 abs_pz = _mm_and_ps(pz, signMask);

#if defined(__FMA__) || defined(__AVX2__)
            // Modern computers can do these calculations even faster.
            __m128 r = _mm_mul_ps(ex, abs_px);
            r = _mm_fmadd_ps(ey, abs_py, r);
            r = _mm_fmadd_ps(ez, abs_pz, r);

            __m128 d = _mm_mul_ps(cx, px);
            d = _mm_fmadd_ps(cy, py, d);
            d = _mm_fmadd_ps(cz, pz, d);
            d = _mm_add_ps(d, pw);
#else
            __m128 r = _mm_mul_ps(ex, abs_px);
            r = _mm_add_ps(r, _mm_mul_ps(ey, abs_py));
            r = _mm_add_ps(r, _mm_mul_ps(ez, abs_pz));

            __m128 d = _mm_mul_ps(cx, px);
            d = _mm_add_ps(d, _mm_mul_ps(cy, py));
            d = _mm_add_ps(d, _mm_mul_ps(cz, pz));
            d = _mm_add_ps(d, pw);
#endif

            // Check if d < -r for any plane (Box is entirely "behind" the plane)
            const __m128 neg_r = _mm_sub_ps(_mm_setzero_ps(), r);
            const __m128 cmp = _mm_cmplt_ps(d, neg_r);

            // Accumulate rejections
            anyReject = _mm_or_ps(anyReject, cmp);
        }

        // Only the first 6 planes are needed (the rest were just for padding).
        const int mask = _mm_movemask_ps(anyReject);
        return (mask & 0x3F) == 0;  // Test only bits 0-5 (6 real planes)

#else
        // Scalar fallback
        for (int i = 0; i < 6; ++i) {
            const float r = extents.x * std::abs(planesX[i]) +
                           extents.y * std::abs(planesY[i]) +
                           extents.z * std::abs(planesZ[i]);
            const float d = center.x * planesX[i] +
                           center.y * planesY[i] +
                           center.z * planesZ[i] + planesW[i];
            if (d < -r) [[unlikely]] return false;
        }
        return true;
#endif
    }

private:
    /// Data is organized for fast loading by the computer.
    alignas(32) float planesX[8] = {};
    alignas(32) float planesY[8] = {};
    alignas(32) float planesZ[8] = {};
    alignas(32) float planesW[8] = {};
};
