#include "Chunk.h"
#include "Blocks.h"

// Helper to create block faces for the world mesh.
void Chunk::generateWorldFaces(const int x, const int y, const int z, FACE_DIRECTION faceDirection, const Block *block,
                               unsigned int &currentVertex, const int subChunkIndex) {
    // Only see-through blocks reach this path (solids and leaves are greedy-meshed), so it feeds the cutout mesh.
    auto& vertices = cutoutVertices[subChunkIndex];
    auto& indices = cutoutIndices[subChunkIndex];
    currentVertex = static_cast<unsigned int>(vertices.size());

    float wx = worldPos.x;
    float wy = worldPos.y;
    float wz = worldPos.z;

    uint16_t layerIndex;
    if (faceDirection == TOP) {
        layerIndex = block->topLayer;
    } else if (faceDirection == BOTTOM) {
        layerIndex = block->bottomLayer;
    } else {
        layerIndex = block->sideLayer;
    }

    switch (faceDirection) {
    case NORTH:
        vertices.emplace_back(x + 1 + wx, y + wy, z + wz, 1, 0, NORTH, layerIndex);
        vertices.emplace_back(x + wx, y + wy, z + wz, 0, 0, NORTH, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + 1 + wy, z + wz, 1, 1, NORTH, layerIndex);
        vertices.emplace_back(x + wx, y + 1 + wy, z + wz, 0, 1, NORTH, layerIndex);
        break;
    case SOUTH:
        vertices.emplace_back(x + wx, y + wy, z + 1 + wz, 0, 0, SOUTH, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + wy, z + 1 + wz, 1, 0, SOUTH, layerIndex);
        vertices.emplace_back(x + wx, y + 1 + wy, z + 1 + wz, 0, 1, SOUTH, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + 1 + wy, z + 1 + wz, 1, 1, SOUTH, layerIndex);
        break;
    case WEST:
        vertices.emplace_back(x + wx, y + wy, z + wz, 0, 0, WEST, layerIndex);
        vertices.emplace_back(x + wx, y + wy, z + 1 + wz, 1, 0, WEST, layerIndex);
        vertices.emplace_back(x + wx, y + 1 + wy, z + wz, 0, 1, WEST, layerIndex);
        vertices.emplace_back(x + wx, y + 1 + wy, z + 1 + wz, 1, 1, WEST, layerIndex);
        break;
    case EAST:
        vertices.emplace_back(x + 1 + wx, y + wy, z + 1 + wz, 1, 0, EAST, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + wy, z + wz, 0, 0, EAST, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + 1 + wy, z + 1 + wz, 1, 1, EAST, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + 1 + wy, z + wz, 0, 1, EAST, layerIndex);
        break;
    case TOP:
        vertices.emplace_back(x + wx, y + 1 + wy, z + 1 + wz, 0, 0, TOP, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + 1 + wy, z + 1 + wz, 1, 0, TOP, layerIndex);
        vertices.emplace_back(x + wx, y + 1 + wy, z + wz, 0, 1, TOP, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + 1 + wy, z + wz, 1, 1, TOP, layerIndex);
        break;
    case BOTTOM:
        vertices.emplace_back(x + 1 + wx, y + wy, z + 1 + wz, 1, 1, BOTTOM, layerIndex);
        vertices.emplace_back(x + wx, y + wy, z + 1 + wz, 0, 1, BOTTOM, layerIndex);
        vertices.emplace_back(x + 1 + wx, y + wy, z + wz, 1, 0, BOTTOM, layerIndex);
        vertices.emplace_back(x + wx, y + wy, z + wz, 0, 0, BOTTOM, layerIndex);
        break;
    default:
        break;
    }

    indices.push_back(currentVertex + 0);
    indices.push_back(currentVertex + 3);
    indices.push_back(currentVertex + 1);
    indices.push_back(currentVertex + 0);
    indices.push_back(currentVertex + 2);
    indices.push_back(currentVertex + 3);
    currentVertex += 4;
}

// Logic for drawing plants and other thin objects.
void Chunk::generateBillboardFaces(const int x, const int y, const int z, const Block *block,
                                   unsigned int &currentVertex, const int subChunkIndex) {
    auto& vertices = billboardVertices[subChunkIndex];
    auto& indices = billboardIndices[subChunkIndex];

    float wx = worldPos.x;
    float wy = worldPos.y;
    float wz = worldPos.z;

    uint16_t layerIndex = block->sideLayer;

    // Random rotation based on position to break the "X" grid pattern
    float randomAngle = (float)((x * 12345 + z * 67890 + y * 444) % 360) * (3.14159f / 180.0f);
    
    float cosA = cos(randomAngle);
    float sinA = sin(randomAngle);

    auto rotate = [&](float dx, float dz) {
        return glm::vec2(dx * cosA - dz * sinA, dx * sinA + dz * cosA);
    };

    // Calculate rotated corners relative to center
    // We normally go from 0 to 1. Center is 0.5, 0.5.
    // P00 (0,0) -> dl = -0.5, -0.5 (Bottom Left relative to center)
    // P11 (1,1) -> dl = 0.5, 0.5   (Top Right relative to center)
    // P01 (0,1) -> dl = -0.5, 0.5  (Top Left relative to center)
    // P10 (1,0) -> dl = 0.5, -0.5  (Bottom Right relative to center)
    
    glm::vec2 p00 = rotate(-0.5f, -0.5f);
    glm::vec2 p11 = rotate(0.5f, 0.5f);
    glm::vec2 p01 = rotate(-0.5f, 0.5f);
    glm::vec2 p10 = rotate(0.5f, -0.5f);

    float cx = x + wx + 0.5f;
    float cz = z + wz + 0.5f;

    // Quad 1: P00 to P11
    
    // Fetch light level
    const int MAX_LIGHT_IDX = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH;
    int lightIdx = x * CHUNK_WIDTH * CHUNK_HEIGHT + z * CHUNK_HEIGHT + y;
    uint8_t light = (lightIdx >= 0 && lightIdx < MAX_LIGHT_IDX) ? lightMap[lightIdx] : 0;

    // Bottom Left (P00)
    vertices.emplace_back(cx + p00.x, y + wy, cz + p00.y, 0, 0, layerIndex, light);
    // Bottom Right (P11)
    vertices.emplace_back(cx + p11.x, y + wy, cz + p11.y, 1, 0, layerIndex, light);
    // Top Left (P00)
    vertices.emplace_back(cx + p00.x, y + 1 + wy, cz + p00.y, 0, 1, layerIndex, light);
    // Top Right (P11)
    vertices.emplace_back(cx + p11.x, y + 1 + wy, cz + p11.y, 1, 1, layerIndex, light);

    // Quad 2: P01 to P10
    // Bottom Left (P01)
    vertices.emplace_back(cx + p01.x, y + wy, cz + p01.y, 0, 0, layerIndex, light);
    // Bottom Right (P10)
    vertices.emplace_back(cx + p10.x, y + wy, cz + p10.y, 1, 0, layerIndex, light);
    // Top Left (P01)
    vertices.emplace_back(cx + p01.x, y + 1 + wy, cz + p01.y, 0, 1, layerIndex, light);
    // Top Right (P10)
    vertices.emplace_back(cx + p10.x, y + 1 + wy, cz + p10.y, 1, 1, layerIndex, light);

    indices.push_back(currentVertex + 0);
    indices.push_back(currentVertex + 3);
    indices.push_back(currentVertex + 1);
    indices.push_back(currentVertex + 0);
    indices.push_back(currentVertex + 2);
    indices.push_back(currentVertex + 3);

    indices.push_back(currentVertex + 4);
    indices.push_back(currentVertex + 7);
    indices.push_back(currentVertex + 5);
    indices.push_back(currentVertex + 4);
    indices.push_back(currentVertex + 6);
    indices.push_back(currentVertex + 7);

    currentVertex += 8;
}

// Logic for drawing liquids like water or lava.
void Chunk::generateLiquidFaces(const int x, const int y, const int z, const FACE_DIRECTION faceDirection,
                                const Block *block, unsigned int &currentVertex, char liquidTopValue, uint8_t light, const int subChunkIndex) {
    auto& vertices = liquidVertices[subChunkIndex];
    auto& indices = liquidIndices[subChunkIndex];

    float wx = worldPos.x;
    float wy = worldPos.y;
    float wz = worldPos.z;

    uint16_t layerIndex = block->sideLayer; 
    
    // Liquid height adjustment
    float topY = (faceDirection == TOP) ? 0.875f : 1.0f;
    if (liquidTopValue == 1) topY = 1.0f;

    // Light is passed in now (correctly handled for neighbors)
    // Lava is self-illuminated. It should not depend on neighbor light for emission.
    if (block->id == Blocks::LAVA) {
         light = (light & 0xF0) | 15;
    }

    switch (faceDirection) {
    case NORTH:
        vertices.emplace_back(x + 1 + wx, y + wy, z + wz, 1, 0, NORTH, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + wx, y + topY + wy, z + wz, 0, 1, NORTH, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + 1 + wx, y + topY + wy, z + wz, 1, 1, NORTH, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + wx, y + wy, z + wz, 0, 0, NORTH, layerIndex, liquidTopValue, light);
        break;
    case SOUTH:
        vertices.emplace_back(x + wx, y + wy, z + 1 + wz, 0, 0, SOUTH, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + 1 + wx, y + topY + wy, z + 1 + wz, 1, 1, SOUTH, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + wx, y + topY + wy, z + 1 + wz, 0, 1, SOUTH, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + 1 + wx, y + wy, z + 1 + wz, 1, 0, SOUTH, layerIndex, liquidTopValue, light);
        break;
    case WEST:
        vertices.emplace_back(x + wx, y + wy, z + wz, 0, 0, WEST, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + wx, y + topY + wy, z + 1 + wz, 1, 1, WEST, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + wx, y + topY + wy, z + wz, 0, 1, WEST, layerIndex, liquidTopValue, light);
        vertices.emplace_back(x + wx, y + wy, z + 1 + wz, 1, 0, WEST, layerIndex, liquidTopValue, light);
        break;
    case EAST:
        // Target: v0(RB), v1(TL), v2(TR), v3(BL)
        // Look from +X. Right is Z+1. Left is Z.
        // RB: y, z+1.
        vertices.emplace_back(x + 1 + wx, y + wy, z + 1 + wz, 1, 0, EAST, layerIndex, liquidTopValue, light); // v0 (RB)
        // TL: y+t, z.
        vertices.emplace_back(x + 1 + wx, y + topY + wy, z + wz, 0, 1, EAST, layerIndex, liquidTopValue, light); // v1 (TL)
        // TR: y+t, z+1.
        vertices.emplace_back(x + 1 + wx, y + topY + wy, z + 1 + wz, 1, 1, EAST, layerIndex, liquidTopValue, light); // v2 (TR)
        // BL: y, z.
        vertices.emplace_back(x + 1 + wx, y + wy, z + wz, 0, 0, EAST, layerIndex, liquidTopValue, light); // v3 (BL)
        break;
    case TOP:
        // Target: +Y.
        // RB, TL, TR, BL.
        // Look from +Y. Right is X. Up is Z.
        // RB: x+1, z. (Wait, Coord sys: Right is +X, Up is +Z?)
        // v0(RB): x+1, z+1?
        // Let's test 0,1,2 (RB, TL, TR).
        // RB(1,1) -> TL(0,0) -> TR(1,0).
        // RB-TL (-1,-1). RB-TR (0,-1).
        // Cross (-1,-1) x (0,-1) = (1 - 0) = +1. +Y. Correct.
        
        // So we need:
        // v0 (RB): x+1, z+1.
        vertices.emplace_back(x + 1 + wx, y + topY + wy, z + 1 + wz, 1, 0, TOP, layerIndex, liquidTopValue, light);
        // v1 (TL): x, z.
        vertices.emplace_back(x + wx, y + topY + wy, z + wz, 0, 1, TOP, layerIndex, liquidTopValue, light);
        // v2 (TR): x+1, z.
        vertices.emplace_back(x + 1 + wx, y + topY + wy, z + wz, 1, 1, TOP, layerIndex, liquidTopValue, light);
        // v3 (BL): x, z+1.
        vertices.emplace_back(x + wx, y + topY + wy, z + 1 + wz, 0, 0, TOP, layerIndex, liquidTopValue, light);
        break;
    case BOTTOM:
        // Target: -Y.
        // RB, TL, TR, BL.
        // Look from -Y (Under). Right is X. Up is Z (Wait, Up is +Z?).
        // If Y is up. Looking up.
        // X is Right. Z is Up?
        // Normal -Y.
        // Tri 0,1,2 (RB, TL, TR). Cross -Y.
        // Order: RB, TL, TR -> CCW?
        // RB(1,1) -> TL(0,0) -> TR(1,0).
        // Cross +1. +Y.
        // Wait, we generate +Y normal with this winding.
        // So we need FLIPPED Geometry for Bottom?
        // Or standard produces +N.
        // If we want -Y, we need Back Face?
        // No, Standard Index 0,1,2 produces +N.
        // So for Bottom, we want -Y.
        // So 0,1,2 is Back Face.
        // So we need to Swap Columns?
        // Or just map vertices so that 0,1,2 produces -Y.
        // 0(RB), 1(TL), 2(TR).
        // RB-TL-TR producing -Y.
        // Need Clockwise?
        // Swap Left/Right logic?
        
        // Or keep Standard indices, but define v0,v1,v2 such that they wind correctly.
        // Desired Normal: -Y.
        // v0->v1->v2 (Cross should be -Y).
        // v0(1,1), v1(0,0), v2(0,1).
        // v0-v1 (-1,-1). v0-v2 (-1,0).
        // Cross (-1,-1)x(-1,0) = (0 - 1) = -1. (-Y).
        // So v0(1,1), v1(0,0), v2(0,1).
        // v0: x+1, z+1. (RB).
        // v1: x, z. (TL).
        // v2: x, z+1. (LB/BL).
        // So for Bottom, v2 is BL?
        // Standard says v2 is TR.
        // So for Bottom, we assign TR vertex to BL position?
        // Let's just write explicit coords that work.
        
        // v0: x+1, z+1.
        vertices.emplace_back(x + 1 + wx, y + wy, z + 1 + wz, 1, 1, BOTTOM, layerIndex, liquidTopValue, light);
        // v1: x, z.
        vertices.emplace_back(x + wx, y + wy, z + wz, 0, 0, BOTTOM, layerIndex, liquidTopValue, light);
        // v2: x, z+1.
        vertices.emplace_back(x + wx, y + wy, z + 1 + wz, 1, 0, BOTTOM, layerIndex, liquidTopValue, light);
        // v3: x+1, z.
        vertices.emplace_back(x + 1 + wx, y + wy, z + wz, 0, 1, BOTTOM, layerIndex, liquidTopValue, light);
        break;
    default:
        break;
    }

    // Indices for two triangles (Standardized to ensure CCW winding relative to face normal)
    // We need to ensure that both triangles face OUTWARD.
    // Based on vertex arrangement per face:
    
    // NORTH:
    // v0: RB, v1: LT, v2: RT, v3: LB
    // Tri 1 (0,3,1): RB->LB->LT (CCW from back? Normal -Z). Correct.
    // Tri 2: Was (0,2,3) -> RB->RT->LB (CW? Normal +Z). Wrong.
    // Fix Tri 2 to (0,1,2): RB->LT->RT (CCW? Normal -Z). 
    // Let's check (0,1,2): RB(1,0)->LT(0,1)->RT(1,1).
    // RB-LT (-1,1). RB-RT (0,1). Cross -1. -Z. Correct.
    // Or (2,1,0) ??
    
    // Actually, let's just use a consistent fan or strip if possible.
    // v3(LB), v0(RB), v2(RT), v1(LT).
    // Quad.
    // 3,0,1 (LB, RB, LT) -> Normal -Z.
    // 1,0,2 (LT, RB, RT) -> Normal -Z.
    // Map to indices:
    // v0=0, v1=1, v2=2, v3=3.
    // 3,0,1 -> 3,0,1.
    // 1,0,2 -> 1,0,2.
    
    // But verify vertices for EACH face case since they are pushed differently.
    
    // NORTH: v0(br), v1(tl), v2(tr), v3(bl).
    // 3(bl), 0(br), 1(tl) -> Correct (-Z).
    // 1(tl), 0(br), 2(tr) -> Correct (-Z).
    // So indices: 3, 0, 1 and 1, 0, 2. (Or 0,2,1).
    // Existing: 0,3,1 (Same order as 3,0,1).
    // Existing 2: 0,2,3. Wrong.
    // New 2: 0,1,2 (BR, TL, TR). Cross -Z. Correct.
    
    indices.push_back(currentVertex + 0);
    indices.push_back(currentVertex + 3); // 3
    indices.push_back(currentVertex + 1); // 1

    indices.push_back(currentVertex + 0);
    indices.push_back(currentVertex + 1); // 1
    indices.push_back(currentVertex + 2); // 2
    
    /* 
       Wait, this "Indices" block is shared for ALL faces.
       This means the Vertex Pushing Order MUST be consistent for all faces 
       such that 0,3,1 and 0,1,2 produce correct normals.
       
       Let's check SOUTH.
       Normal +Z.
       v0(LB, z+1), v1(TR, z+1+t?), v2(TL, z+1+t?), v3(RB, z+1). 
       Wait, let's read SOUTH code.
       v0: x, y, z+1. (Left Bot). LB.
       v1: x+1, y+t, z+1. (Right Top). RT.
       v2: x, y+t, z+1. (Left Top). LT.
       v3: x+1, y, z+1. (Right Bot). RB.
       
       Target Normal: +Z.
       Tri 1 (0,3,1): LB->RB->RT. 
       LB(0,0)->RB(1,0)->RT(1,1).
       LB-RB (1,0). LB-RT (1,1). Cross +1. +Z. Correct.
       
       Tri 2 (0,1,2): LB->RT->LT.
       LB(0,0)->RT(1,1)->LT(0,1).
       LB-RT (1,1). LB-LT (0,1). Cross +1. +Z. Correct.
       
       So Indices (0,3,1) and (0,1,2) Work for SOUTH too!
       
       Check WEST (-X).
       v0: x, y, z. (Back Bot). BB.
       v1: x, y+t, z+1. (Front Top). FT.
       v2: x, y+t, z. (Back Top). BT.
       v3: x, y, z+1. (Front Bot). FB.
       
       Target Normal: -X.
       Tri 1 (0,3,1): BB->FB->FT.
       BB(0,0)->FB(1,0 Z)->FT(1,1 ZY).
       BB-FB (0,0,1). BB-FT (0,1,1).
       Cross (0,0,1)x(0,1,1) -> (0*1 - 1*1, ...) = -1 X. (-X). Correct.
       
       Tri 2 (0,1,2): BB->FT->BT.
       BB(0,0)->FT(1,1)->BT(0,1).
       BB-FT (0,1,1). BB-BT (0,1,0).
       Cross (0,1,1)x(0,1,0) -> (-1, 0, 0). -X. Correct.
       
       So Indices (0,3,1) and (0,1,2) Work for WEST!
       
       Check EAST (+X).
       v0: x+1, y, z+1. (Front Bot). FB.
       v1: x+1, y, z. (Back Bot). BB.
       v2: x+1, y+t, z+1. (Front Top). FT.
       v3: x+1, y+t, z. (Back Top). BT.
       
       Wait, order is strange here.
       v0: z+1 (F).
       v1: z (B).
       v2: z+1 (F). top.
       v3: z (B). top.
       
       v0: FB.
       v1: BB.
       v2: FT.
       v3: BT.
       
       Target +X.
       Tri 1 (0,3,1): FB->BT->BB.
       FB(1,0)->BT(0,1)->BB(0,0). (Coords in Z,Y).
       FB-BT (-1,1). FB-BB (-1,0).
       Cross (-1,1)x(-1,0) = (1) -> +X?
       Normal Vector calculation:
       (dZ, dY).
       U = BT-FB = (-1, 1).
       V = BB-FB = (-1, 0).
       UxV = (-1*0 - 1*-1) = +1. (+X). Correct.
       
       Tri 2 (0,1,2): FB->BB->FT. 
       0,1,2 -> FB, BB, FT.
       FB, BB, FT.
       FB-BB (-1,0). FB-FT (0,1).
       Cross (-1,0)x(0,1) = -1. (-X). 
       WRONG. Should be +X.
       
       So EAST vertex order is suspicious or inconsistent with 0,1,2 pattern.
       EAST vIndices:
       v0: z+1, y
       v1: z, y
       v2: z+1, y+t
       v3: z, y+t
       
       Let's swap East vertices to match pattern?
       Pattern seems to be: 
       0: Bottom Right (relative to face normal)
       1: Top Right? or Top Left?
       
       Let's just fix INDICES to (0,3,1) and (0,1,2) and Adjust Vertices for EAST.
       
       To get +X from (0,1,2):
       0(FB), 1(Should be FT?), 2(Should be BB?).
       FB->FT->BB. Cross: (0,1)x(-1,0) = +1.
       
       So Mapping:
       0: FB. (Existing v0)
       1: FT. (Existing v2).
       2: BB. (Existing v1).
       3: BT. (Existing v3).
       
       So I must change EAST vertex push order to:
       v0 (FB), v1 (FT), v2 (BB), v3 (BT)?
       Then (0,3,1) becomes FB->BT->FT. (0,1)-(0,1) -> 0 area? No.
       FB(1,0). BT(0,1). FT(1,1).
       FB->BT (-1,1). FB->FT (0,1). Cross -1 (-X). Wrong.
       
       Okay, simpler:
       Keep Vertices as they are.
       And define Indices PER FACE.
       
       But the code shares indices at the end.
       So I MUST standardize Vertex Order.
       
       Standard: 
       v0: Bottom Right
       v1: Top Left
       v2: Top Right
       v3: Bottom Left
       (Indices 0,3,1 and 0,1,2 work for this if winding is standard)
       
       Let's check NORTH again.
       v0(BR), v1(TL), v2(TR), v3(BL).
       0,3,1 -> BR, BL, TL. (-Z).
       0,1,2 -> BR, TL, TR. (-Z).
       Matches Standard.
       
       Let's Check SOUTH.
       v0: x, y, z+1 (Left Bot.. wait. Looking from +Z to -Z (South Face).
       Left is x+1. Right is x.
       v0 is at x. So v0 is Right Bot.
       v1: x+1, y+t. Left Top.
       v2: x, y+t. Right Top.
       v3: x+1, y. Left Bot.
       
       Standard: v0(RB), v1(TL), v2(TR), v3(BL).
       Current South Code:
       v0: LB (Local Right). OK.
       v1: RT (Local Left). (Wait. x+1 is Left if looking South). OK.
           So v1 is TL (Local).
       v2: LT (Local Right). RT (Local).
       v3: RB (Local Left). BL (Local).
       So South matches Standard!
       
       Check WEST (-X).
       Looking from -X to +X.
       Right is Z (Front). Left is Z+1 (Back)?
       No. Z is Right. Z+1 is Left?
       Right Hand Rule. -X. Y up. Z is Right.
       v0: x, y, z. (Right Bot). Correct.
       v1: x, y+t, z+1. (Left Top). Correct.
       v2: x, y+t, z. (Right Top). Correct.
       v3: x, y, z+1. (Left Bot). Correct.
       Matches Standard!
       
       Check EAST (+X).
       Looking from +X to -X.
       Right is Z+1. Left is Z.
       v0: x+1, y, z+1. (Right Bot). Correct.
       v1: x+1, y, z. (Left Bot ?). NO. Left is Z.
           So v1 is Left Bot.
           Standard wants v1 to be TL.
       v2: x+1, y+t, z+1. (Right Top). TR.
           Standard wants v2 to be TR. OK.
       v3: x+1, y+t, z. (Left Top). TL.
           Standard wants v3 to be BL.
           
       So EAST is Mixed Up.
       Current East:
       v0: RB.
       v1: LB.  <-- Mismatch
       v2: RT.
       v3: LT.  <-- Mismatch
       
       Need to swap v1 and v3 for East?
       No, Standard is: v0(RB), v1(TL), v2(TR), v3(BL).
       Current East:
       v1 is LB.
       v3 is LT.
       
       I need:
       v1 to be LT.
       v3 to be LB.
       
       So Swap v1 and v3 logic in EAST switch case.
       
       Then check TOP/BOTTOM.
       
       TOP (+Y).
       Looking down.
       Right is X. Up is Z?
       v0: x, z+1. (Left Top?)
       Standard: RB, TL, TR, BL.
       
       This is getting complicated to re-verify all.
       But I know indices 0,3,1 and 0,1,2 work for North, South, West.
       And East just needs vertex fix.
       
       So I will:
       1. Replace Index push at end with (0,3,1) and (0,1,2).
       2. Fix EAST vertices to match Standard (0:RB, 1:TL, 2:TR, 3:BL).
       3. Fix TOP/BOTTOM vertices to match Standard.
       
    */
    
    // Applying the 0,3,1 | 0,1,2 indices replacement at the end of function.
    // I will replace the indices push_back call.


    currentVertex += 4;
}