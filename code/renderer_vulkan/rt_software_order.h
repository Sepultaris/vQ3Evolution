#ifndef RT_SOFTWARE_ORDER_H
#define RT_SOFTWARE_ORDER_H

/* Eight stackless traversal orders, indexed by ray direction signs. Each word
 * holds a subtree's escape node and a high bit selecting the right child first.
 * Ordering changes candidate discovery, never bounds or triangle acceptance. */
#define RT_SW_ORDER_COUNT 8u
#define RT_SW_RIGHT_FIRST 0x80000000u

static void rt_sw_order_node(const rt_bvh_node_t *nodes, uint32_t node,
    uint32_t *links, const uint32_t escape[RT_SW_ORDER_COUNT])
{
    const rt_bvh_node_t *n = nodes + node;
    if (n->right & RT_BVH_LEAF_FLAG) {
        for (uint32_t octant = 0; octant < RT_SW_ORDER_COUNT; ++octant)
            links[node * RT_SW_ORDER_COUNT + octant] = escape[octant];
        return;
    }
    const rt_bvh_node_t *left = nodes + n->left, *right = nodes + n->right;
    float delta[3];
    uint32_t axis = 0, left_escape[RT_SW_ORDER_COUNT], right_escape[RT_SW_ORDER_COUNT];
    for (uint32_t a = 0; a < 3; ++a) {
        delta[a] = (right->mn[a] + right->mx[a]) - (left->mn[a] + left->mx[a]);
        if (fabsf(delta[a]) > fabsf(delta[axis])) axis = a;
    }
    for (uint32_t octant = 0; octant < RT_SW_ORDER_COUNT; ++octant) {
        int right_first = ((octant >> axis) & 1u) != (delta[axis] < 0);
        links[node * RT_SW_ORDER_COUNT + octant] = escape[octant] |
            (right_first ? RT_SW_RIGHT_FIRST : 0u);
        left_escape[octant] = right_first ? escape[octant] : n->right;
        right_escape[octant] = right_first ? n->left : escape[octant];
    }
    rt_sw_order_node(nodes, n->left, links, left_escape);
    rt_sw_order_node(nodes, n->right, links, right_escape);
}

static void rt_sw_order_tree(const rt_bvh_node_t *nodes, uint32_t root,
    uint32_t end, uint32_t *links)
{
    uint32_t escape[RT_SW_ORDER_COUNT];
    if (root == end) return;
    for (uint32_t i = 0; i < RT_SW_ORDER_COUNT; ++i) escape[i] = end;
    rt_sw_order_node(nodes, root, links, escape);
}
#endif
