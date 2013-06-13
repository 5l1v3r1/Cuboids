#include "cuboid_qtmoves.h"

static MoveMap _qt_move_map(CuboidMovesAxis axis, int offset);
static SliceMap _qt_slice_map(CuboidMovesAxis axis);
static int _qt_face_for_face_turn(CuboidMovesAxis axis, int offset);
static int _qt_face_side_length(CuboidDimensions dim, CuboidMovesAxis axis);
static void _qt_face_center_dimensions(CuboidDimensions dim, int face, int * w, int * h);

static void _qt_rotate_corners(Cuboid * out, CuboidMovesAxis axis, int offset);
static void _qt_rotate_edges(Cuboid * out, CuboidMovesAxis axis, int offset);
static void _qt_rotate_centers(Cuboid * out, CuboidMovesAxis axis, int offset);

// gives the index of a center piece on a center line
static int _qt_center_line_index(CuboidDimensions dim,
                                 SliceMap sliceMap,
                                 int centerIndex, // 0-3
                                 int lineIndex,
                                 int indexInLine);
static int _qt_center_line_length(CuboidDimensions dim,
                                  SliceMap sliceMap);
static void _qt_slice_edges(Cuboid * out, CuboidMovesAxis axis, int layer);
static void _qt_slice_center(Cuboid * out, CuboidMovesAxis axis, int layer); 

int cuboid_validate_quarter_turn(CuboidDimensions dimensions,
                                 CuboidMovesAxis axis) {
    if (axis == CuboidMovesAxisX) {
        return (dimensions.y == dimensions.z);
    } else if (axis == CuboidMovesAxisY) {
        return (dimensions.x == dimensions.z);
    } else {
        return (dimensions.x == dimensions.y);
    }
}

Cuboid * cuboid_generate_quarter_face_turn(CuboidDimensions dimensions,
                                           CuboidMovesAxis axis,
                                           int offset) {
    assert(cuboid_validate_quarter_turn(dimensions, axis));
    assert(offset == -1 || offset == 1);
    Cuboid * out = cuboid_create(dimensions);
    _qt_rotate_corners(out, axis, offset);
    if (cuboid_count_edges(out) > 0) {
        _qt_rotate_edges(out, axis, offset);
    }
    if (cuboid_count_centers(out) > 0) {
        _qt_rotate_centers(out, axis, offset);
    }
    return out;
}

Cuboid * cuboid_generate_quarter_slice(CuboidDimensions dimensions,
                                      CuboidMovesAxis axis,
                                      int layer) {
    assert(cuboid_validate_quarter_turn(dimensions, axis));
    int edgeSize;
    if (axis == CuboidMovesAxisX) edgeSize = dimensions.x - 2;
    if (axis == CuboidMovesAxisY) edgeSize = dimensions.y - 2;
    if (axis == CuboidMovesAxisZ) edgeSize = dimensions.z - 2;
    assert(layer >= 0 && layer < edgeSize);
    Cuboid * out = cuboid_create(dimensions);
    _qt_slice_edges(out, axis, layer);
    _qt_slice_center(out, axis, layer);
    return out;
}

/***********
 * PRIVATE *
 ***********/

static MoveMap _qt_move_map(CuboidMovesAxis axis, int offset) {
    int indexes[3][2] = {{0, 1}, {3, 2}, {5, 4}};
    int index = indexes[axis][(offset + 1) / 2];
    return MovesTable[index];
}

static SliceMap _qt_slice_map(CuboidMovesAxis axis) {
    return SlicesTable[axis];
}

static int _qt_face_for_face_turn(CuboidMovesAxis axis, int offset) {
    int indexes[3][2] = {{6, 5}, {4, 3}, {2, 1}};
    return indexes[axis][(offset + 1) / 2];
}

static int _qt_face_side_length(CuboidDimensions dim, CuboidMovesAxis axis) {
    // this method assumes that the turn has already been validated
    if (axis != CuboidMovesAxisX) return dim.x;
    return dim.y;
}

static void _qt_face_center_dimensions(CuboidDimensions dim, int face, int * w, int * h) {
    if (face == 1 || face == 2) {
        *w = dim.x - 2;
        *h = dim.y - 2;
    } else if (face == 3 || face == 4) {
        *w = dim.x - 2;
        *h = dim.z - 2;
    } else {
        *w = dim.z - 2;
        *h = dim.y - 2;
    }
}

/**************
 * Face turns *
 **************/

static void _qt_rotate_corners(Cuboid * out, CuboidMovesAxis axis, int offset) {
    int i;
    MoveMap map = _qt_move_map(axis, offset);
    for (i = 0; i < 4; i++) {
        int cornerIndex = map.corners[i];
        CuboidCorner corner;
        corner.symmetry = map.symmetry;
        corner.index = map.cornerPieces[i];
        out->corners[cornerIndex] = corner;
    }
}

static void _qt_rotate_edges(Cuboid * out, CuboidMovesAxis axis, int offset) {
    // this method assumes that all dedges on the face have the same number
    // of edges; this is a safe assumption because the face has been
    // validated for a quarter turn.
    
    // some notes for those who are getting confused (i.e. me):
    // - dedgeIndex is the physical slot, dedgePiece is the physical piece
    // - j is the physical slot, edgePiece is the physical piece
    // this is extra confusing because an edge has two coordinates!
    
    int i;
    MoveMap map = _qt_move_map(axis, offset);
    for (i = 0; i < 4; i++) {
        int dedgeIndex = map.dedges[i];
        int dedgePiece = map.dedgePieces[i];
        int j, dedgeSize = cuboid_count_edges_for_dedge(out, dedgeIndex);
                
        // place each edge
        for (j = 0; j < dedgeSize; j++) {
            int edgePiece = map.dedgesReorder[i] ? dedgeSize - j - 1 : j;
            int offset = cuboid_edge_index(out, dedgeIndex, j);
            CuboidEdge edge;
            edge.edgeIndex = edgePiece;
            edge.dedgeIndex = dedgePiece;
            edge.symmetry = map.symmetry;            
            out->edges[offset] = edge;
        }
    }
}

static void _qt_rotate_centers(Cuboid * out, CuboidMovesAxis axis, int offset) {
    // some clarity for those who are confused:
    // this function goes through each center piece on the identity,
    // rotates it, and then writes it to its new position on the output.
    
    int face = _qt_face_for_face_turn(axis, offset);
    int centerSize = _qt_face_side_length(out->dimensions, axis) - 2;
    int i;
    for (i = 0; i < centerSize * centerSize; i++) {
        int x, y;
        qt_center_point_from_index(centerSize, i, &x, &y);        
        qt_rotate_center_point(face, &x, &y);
        int destIndex = qt_center_point_to_index(centerSize, x, y);
        
        int centerIndex = cuboid_center_index(out, face, destIndex);
        CuboidCenter c;
        c.side = face;
        c.index = i;
        out->centers[centerIndex] = c;
    }
}

/**********
 * Slices *
 **********/

static int _qt_center_line_index(CuboidDimensions dim,
                                 SliceMap sliceMap,
                                 int centerIndex, // 0-3
                                 int lineIndex,
                                 int indexInLine) {
    int w, h;
    _qt_face_center_dimensions(dim, sliceMap.centers[centerIndex], &w, &h);
    
    int xValue = 0, yValue = 0;
    if (sliceMap.lineAxis[centerIndex] == 0) {
        // the line is horizontal
        if (sliceMap.flipVert[centerIndex]) {
            yValue = h - lineIndex - 1;
        } else yValue = lineIndex;
        if (sliceMap.flipHoriz[centerIndex]) {
            xValue = w - indexInLine - 1;
        } else xValue = indexInLine;
    } else {
        // the line is vertical
        if (sliceMap.flipHoriz[centerIndex]) {
            xValue = w - lineIndex - 1;
        } else xValue = lineIndex;
        if (sliceMap.flipVert[centerIndex]) {
            yValue = h - indexInLine - 1;
        } else yValue = indexInLine;
    }
    return (yValue * w) + xValue;
}

static int _qt_center_line_length(CuboidDimensions dim,
                                  SliceMap sliceMap) {
    // this method assumes that the slice map is completely
    // valid and thus that every center would yield the same line.
    int w, h;
    _qt_face_center_dimensions(dim, sliceMap.centers[0], &w, &h);
    int face = sliceMap.centers[0];
    if (sliceMap.lineAxis[0] == 0) {
        return w;
    } else return h;
}

static void _qt_slice_edges(Cuboid * out, CuboidMovesAxis axis, int layer) {
    int i;
    SliceMap map = _qt_slice_map(axis);
    for (i = 0; i < 4; i++) {
        uint8_t physicalSlot = map.dedges[i];
        uint8_t physicalDedge;
        if (i > 0) physicalDedge = map.dedges[i - 1];
        else physicalDedge = map.dedges[3];
        
        CuboidEdge edge;
        edge.edgeIndex = layer;
        edge.dedgeIndex = physicalDedge;
        edge.symmetry = map.symmetry;
        
        int absIndex = cuboid_edge_index(out, physicalSlot, layer);
        out->edges[absIndex] = edge;
    }
}

static void _qt_slice_center(Cuboid * out, CuboidMovesAxis axis, int layer) {
    int i;
    SliceMap map = _qt_slice_map(axis);
    int numCenters = _qt_center_line_length(out->dimensions, map);
    for (i = 0; i < 4; i++) {
        // i is the physical slot, sourceCenter is the piece
        uint8_t sourceCenter = (i == 0 ? 3 : i - 1);
        int j;
        for (j = 0; j < numCenters; j++) {
            // dest is physical slot, source is piece
            int destIndex = _qt_center_line_index(out->dimensions, map, i, layer, j);
            int sourceIndex = _qt_center_line_index(out->dimensions, map,
                                                    sourceCenter, layer, j);
            int absDest = cuboid_center_index(out, map.centers[i], destIndex);
            CuboidCenter center;
            center.side = map.centers[sourceCenter];
            center.index = sourceIndex;
            out->centers[absDest] = center;
        }
    }
}
