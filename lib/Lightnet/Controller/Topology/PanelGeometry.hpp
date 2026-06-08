#pragma once

// ============================================================================
// PanelGeometry — planar (x,y) layout of the discovered panel tree.
//
// Panels are regular polygons, so the whole network's flat layout is computable
// from the topology alone (no per-device setup, no protocol change): walk the
// tree from an anchor panel and place each child against the edge it shares with
// its parent. This is a faithful port of the mobile visualizer's
// PanelsLayoutService (lightnet-mobile) — same edge-chaining, same move/rotate,
// same rotation convention — so an angle picked against the visualizer maps to
// the same physical sweep axis on the device.
//
// The layout backs the *geometric* directionality field for controller-side
// runners (computeGeometricField below): project each panel's centroid onto an
// axis at a chosen angle, instead of using graph hop-distance (see PanelField.hpp).
//
// Anchored at the lowest panel index (matching the visualizer, which roots at the
// lowest panelId) so the absolute frame — and therefore the angle — agrees with
// what the user sees while authoring. The logical root used for selectors and the
// hop-distance field does NOT affect geometry.
//
// Built on top of a PanelGraph (the root-independent raw adjacency, shared with
// TopologyIndex — see PanelGraph.hpp): this view adds only its own root-dependent
// BFS/layout state on top of the borrowed graph.
//
// Pure C++ → natively unit-testable. Controller-side float; this is outside the
// refgen/Kotlin bit-exact contract (that governs panel-local AnimationPlayer.cpp only).
// ============================================================================

#include <stdint.h>
#include <math.h>
#include "PanelGraph.hpp"    // PanelGraph
#include "TopologyIndex.hpp" // TopologyIndex, PanelSet (computeGeometricCenterField)
#include "PanelField.hpp"    // buildSourceSet (shared source-set logic for the radial field)

namespace Lightnet {
    class PanelGeometry
    {
        public:
            PanelGeometry() : graph(nullptr), n(0)
            {
            }

            // Build the planar layout on top of a pre-built raw graph.
            //   graph          : the root-independent adjacency, built once and shared
            //                    with other rooted views (e.g. TopologyIndex). Borrowed —
            //                    must outlive this object and stay unchanged while it's
            //                    in use (rebuild both together via SceneTopology::rebuild).
            //   edgeCounts[s]  : polygon side count N for the panel at slot s (≥3 in practice)
            //   anchorPanelIdx : panel placed at the canonical frame; invalid (e.g. 0) → the
            //                    lowest panel index, matching the visualizer.
            // Returns false on malformed input (empty graph).
            bool build(
            const PanelGraph &g,
            const uint8_t *   edgeCounts,
            uint8_t           anchorPanelIdx
            )
            {
                graph = &g;
                n     = g.count();

                if (n == 0) return false;

                for (uint8_t s = 0; s < n; s++) {
                    ec[s] = edgeCounts[s] ? edgeCounts[s] : 1; // guard /0
                }

                uint8_t rootSlot;

                if (!graph->slotOf(anchorPanelIdx, rootSlot)) rootSlot = graph->lowestSlot();

                bfsParents(rootSlot);
                layout();

                return true;
            }

            bool valid()        const
            {
                return n > 0;
            }

            uint8_t count()     const
            {
                return n;
            }

            // Centroid (x,y) of a panel by 1-based index. False if the panel isn't present.
            bool centroidOf(uint8_t panelIndex, float& x, float& y) const
            {
                uint8_t s;

                if (!graph->slotOf(panelIndex, s)) return false;

                x = cxArr[s];
                y = cyArr[s];

                return true;
            }

            // Circumradius (centroid→vertex distance) of a panel's regular polygon, i.e. the radius
            // of its circumscribed disc — used to give the panel a radial *extent* for the ripple
            // field. R = edge / (2·sin(π/N)). 0 if the panel isn't present.
            float circumradiusOf(uint8_t panelIndex) const
            {
                uint8_t s;

                if (!graph->slotOf(panelIndex, s)) return 0.0f;

                uint8_t N = ec[s];

                if (N < 2) return 0.0f;

                return EDGE_LEN / (2.0f * sinf(GEO_PI / (float)N));
            }

        private:
            static const uint8_t MAXN  = LIGHTNET_MAX_PANELS;
            static const uint8_t MAXE  = 8;      // max polygon sides we lay out (panels are 3–5)
            static constexpr float EDGE_LEN = 100.0f;
            static constexpr float GEO_PI   = 3.14159265358979323846f;

            const PanelGraph *graph; // borrowed root-independent adjacency (see build())

            uint8_t n;
            uint8_t ec[MAXN];         // slot → polygon side count

            uint8_t parent[MAXN];          // slot → parent slot (0xFF = root)
            uint8_t parentMyEdge[MAXN];    // this panel's edge facing the parent (its "root edge")
            uint8_t parentPeerEdge[MAXN];  // the parent's edge facing this panel
            uint8_t bfsOrder[MAXN];        // slots in parent-before-child order
            uint8_t bfsCount;

            // Per-panel world transform: world = R(rot)·(local − refLocal) + refWorld,
            // with R = [[cos, sin], [-sin, cos]] (the visualizer's rotation convention).
            float rotDeg[MAXN];
            float refLocalX[MAXN], refLocalY[MAXN];
            float refWorldX[MAXN], refWorldY[MAXN];

            float cxArr[MAXN], cyArr[MAXN];   // panel centroids in world space

            void bfsParents(uint8_t rootSlot)
            {
                bool visited[MAXN];

                for (uint8_t s = 0; s < n; s++) {
                    parent[s]         = 0xFF;
                    parentMyEdge[s]   = 0;
                    parentPeerEdge[s] = 0;
                    visited[s]        = false;
                }

                uint8_t queue[MAXN];
                uint8_t qh = 0, qt = 0;

                bfsCount          = 0;
                visited[rootSlot] = true;
                queue[qt++]       = rootSlot;
                bfsOrder[bfsCount++] = rootSlot;

                while (qh < qt) {
                    uint8_t u = queue[qh++];

                    for (uint8_t i = 0; i < graph->degree(u); i++) {
                        uint8_t v = graph->neighborSlot(u, i);

                        if (visited[v]) continue;

                        visited[v]        = true;
                        parent[v]         = u;
                        parentMyEdge[v]   = graph->neighborPeerEdge(u, i); // v's connector toward u
                        parentPeerEdge[v] = graph->neighborMyEdge(u, i);   // u's connector toward v
                        queue[qt++]       = v;
                        bfsOrder[bfsCount++] = v;
                    }
                }
            }

            // Local polygon vertices for an N-gon: V[0]=(0,0); each edge k spans V[k]→V[k+1]
            // at angle (360/N)·k. V[N] closes back onto V[0]. Matches generateEdgeCoords.
            void localVerts(uint8_t N, float *vx, float *vy) const
            {
                float step = 360.0f / (float)N;
                float x = 0.0f, y = 0.0f;

                vx[0] = 0.0f;
                vy[0] = 0.0f;

                for (uint8_t k = 0; k < N; k++) {
                    float rad = step * (float)k * (GEO_PI / 180.0f);

                    x += EDGE_LEN * cosf(rad);
                    y += EDGE_LEN * sinf(rad);

                    vx[k + 1] = x;
                    vy[k + 1] = y;
                }
            }

            // world = R(rot[s])·(p − refLocal[s]) + refWorld[s].
            void applyT(uint8_t s, float px, float py, float& wx, float& wy) const
            {
                float rad  = rotDeg[s] * (GEO_PI / 180.0f);
                float cosA = cosf(rad);
                float sinA = sinf(rad);
                float dx   = px - refLocalX[s];
                float dy   = py - refLocalY[s];

                wx =  cosA * dx + sinA * dy + refWorldX[s];
                wy = -sinA * dx + cosA * dy + refWorldY[s];
            }

            // Signed angle (deg) from vector (ax,ay) to (bx,by). Matches angleBetween().
            static float angleBetween(float ax, float ay, float bx, float by)
            {
                float cross = ax * by - ay * bx;
                float dot   = ax * bx + ay * by;

                return atan2f(cross, dot) * 180.0f / GEO_PI;
            }

            void layout()
            {
                for (uint8_t i = 0; i < bfsCount; i++) {
                    uint8_t s = bfsOrder[i];
                    uint8_t N = (ec[s] <= MAXE) ? ec[s] : MAXE;

                    float vx[MAXE + 1], vy[MAXE + 1];

                    localVerts(N, vx, vy);

                    if (parent[s] == 0xFF) {
                        // Anchor panel: identity transform → world == local.
                        rotDeg[s]    = 0.0f;
                        refLocalX[s] = 0.0f;
                        refLocalY[s] = 0.0f;
                        refWorldX[s] = 0.0f;
                        refWorldY[s] = 0.0f;
                    } else {
                        uint8_t p   = parent[s];
                        uint8_t ppe = parentPeerEdge[s]; // parent's edge facing this panel
                        uint8_t rme = parentMyEdge[s];   // this panel's edge facing parent

                        if (ppe >= ec[p]) ppe = 0;

                        if (rme >= N)     rme = 0;

                        // Parent's shared edge in world space (start Ps, end Pe).
                        float pvx[MAXE + 1], pvy[MAXE + 1];
                        uint8_t Np = (ec[p] <= MAXE) ? ec[p] : MAXE;

                        localVerts(Np, pvx, pvy);

                        float psx, psy, pex, pey;

                        applyT(p, pvx[ppe], pvy[ppe], psx, psy);
                        applyT(p, pvx[ppe + 1], pvy[ppe + 1], pex, pey);

                        // This panel's root edge in its own local space (start Rs, end Re).
                        float rsx = vx[rme], rsy = vy[rme];
                        float rex = vx[rme + 1], rey = vy[rme + 1];

                        // Align the root-edge start to the parent-edge end, then rotate so the
                        // edges run anti-parallel (same physical seam). T derived in the header.
                        rotDeg[s]    = angleBetween(psx - pex, psy - pey, rex - rsx, rey - rsy);
                        refLocalX[s] = rsx;
                        refLocalY[s] = rsy;
                        refWorldX[s] = pex;
                        refWorldY[s] = pey;
                    }

                    // Centroid = mean of the N polygon vertices, transformed to world space.
                    float lcx = 0.0f, lcy = 0.0f;

                    for (uint8_t k = 0; k < N; k++) {
                        lcx += vx[k];
                        lcy += vy[k];
                    }

                    lcx /= (float)N;
                    lcy /= (float)N;

                    applyT(s, lcx, lcy, cxArr[s], cyArr[s]);
                }
            }
    };

    // Geometric directionality field: project each targeted panel's centroid onto the axis at
    // `angleDeg`, then quantise to integer ring coords 0..maxCoord (parallel to panels[]), so
    // the existing runner envelopes (RunnerMath.hpp) consume it exactly like the hop-distance
    // field. `resolution` is the number of integer bands (the sweep span); pick it on the scale
    // of the graph field's maxDepth so `width` stays comparable across directionality modes.
    // With `reverse`, the axis is flipped. Returns maxCoord (0 ⇒ uniform / no spatial gradient).
    inline uint8_t computeGeometricField(
        const PanelGeometry& geo,
        const uint8_t *      panels,
        uint8_t              panelCount,
        float                angleDeg,
        bool                 reverse,
        uint8_t              resolution,
        uint8_t *            coordOut
    )
    {
        if (resolution == 0) resolution = 1;

        const float GEO_PI = 3.14159265358979323846f;
        float rad    = angleDeg * (GEO_PI / 180.0f);
        float ca     = cosf(rad);
        float sa     = sinf(rad);

        // Two passes (centroidOf is a cheap lookup) so no per-panel scratch array is needed —
        // keeps the ESP8266 stack lean alongside fireStep's own coord[]/panels[] buffers.
        float minP = 0.0f, maxP = 0.0f;
        bool any  = false;

        for (uint8_t i = 0; i < panelCount; i++) {
            float x, y;

            if (!geo.centroidOf(panels[i], x, y)) continue;

            float pr = x * ca + y * sa;

            if (!any || pr < minP) minP = pr;

            if (!any || pr > maxP) maxP = pr;

            any = true;
        }

        float span = maxP - minP;

        if (!any || span <= 1e-4f) {
            for (uint8_t i = 0; i < panelCount; i++) coordOut[i] = 0;

            return 0;
        }

        uint8_t maxC = resolution;

        for (uint8_t i = 0; i < panelCount; i++) {
            float x, y;

            if (!geo.centroidOf(panels[i], x, y)) {
                coordOut[i] = 0;

                continue;
            }

            float t = (x * ca + y * sa - minP) / span; // 0..1
            int c = (int)(t * (float)resolution + 0.5f);

            if (c < 0)            c = 0;

            if (c > (int)maxC)    c = maxC;

            coordOut[i] = reverse ? (uint8_t)(maxC - c) : (uint8_t)c;
        }

        return maxC;
    }

    // Geometric *radial* directionality field — the ripple counterpart to computeGeometricField.
    // Where the axis sweep above is for wave/chase, a ripple has no axis: it expands from a centre.
    //
    // A panel is not a point: it is its circumscribed disc, so it occupies a radial *band* [near,
    // far] = (distance from the source to its centroid) ∓ its circumradius. The growing ring lights
    // a panel while its radius is anywhere in that band, so overlapping panels (overlapping bands)
    // light together and the closer one stays lit until the ring clears its far edge — the ring
    // genuinely intersects panel *surfaces* rather than stepping through one centroid distance at a
    // time. Both `nearOut`/`farOut` are quantised to 0..resolution (parallel to panels[]).
    //
    // The source set chooses the centre(s), mirroring the hop-distance field (PanelField.hpp):
    //   source:root     → one ripple from the root centroid
    //   source:panel:N  → one ripple from panel N
    //   source:leaves   → one ripple per leaf, expanding inward simultaneously (nearest-leaf band)
    // With `reverse`, the bands invert (front collapses toward the source). Returns maxCoord
    // (0 ⇒ uniform: no usable source centroid, or every panel is a source).
    //
    // No per-panel scratch array: two passes, each recomputing the (cheap) nearest-source distance,
    // keeping the ESP8266 stack lean alongside fireStep's own coord[]/panels[] buffers.
    inline uint8_t computeGeometricCenterField(
        const PanelGeometry& geo,
        const TopologyIndex& topo,
        const uint8_t *      panels,
        uint8_t              panelCount,
        uint8_t              srcKind,
        uint8_t              srcArg,
        bool                 reverse,
        uint8_t              resolution,
        uint8_t *            nearOut,
        uint8_t *            farOut
    )
    {
        if (resolution == 0) resolution = 1;

        PanelSet src;

        buildSourceSet(topo, srcKind, srcArg, src);

        const uint8_t n = topo.count();

        // Pass 1: largest far edge over the targeted panels (the span to normalise both edges by).
        float maxFar = 0.0f;
        bool any    = false;

        for (uint8_t i = 0; i < panelCount; i++) {
            float px, py;

            if (!geo.centroidOf(panels[i], px, py)) continue;

            float best = -1.0f;

            for (uint8_t s = 0; s < n; s++) {
                float sx, sy;

                if (!src.test(s) || !geo.centroidOf(topo.panelAt(s), sx, sy)) continue;

                float dx = px - sx, dy = py - sy;
                float d  = sqrtf(dx * dx + dy * dy);

                if (best < 0.0f || d < best) best = d;
            }

            if (best < 0.0f) continue;

            float far = best + geo.circumradiusOf(panels[i]);

            if (!any || far > maxFar) maxFar = far;

            any = true;
        }

        if (!any || maxFar <= 1e-4f) {
            for (uint8_t i = 0; i < panelCount; i++) {
                nearOut[i] = 0;
                farOut[i] = 0;
            }

            return 0;
        }

        const uint8_t maxC = resolution;

        // Pass 2: quantise each panel's [near, far] band to 0..resolution.
        for (uint8_t i = 0; i < panelCount; i++) {
            float px, py;

            if (!geo.centroidOf(panels[i], px, py)) {
                nearOut[i] = 0;
                farOut[i]  = 0;

                continue;
            }

            float best = -1.0f;

            for (uint8_t s = 0; s < n; s++) {
                float sx, sy;

                if (!src.test(s) || !geo.centroidOf(topo.panelAt(s), sx, sy)) continue;

                float dx = px - sx, dy = py - sy;
                float d  = sqrtf(dx * dx + dy * dy);

                if (best < 0.0f || d < best) best = d;
            }

            float c = ((best < 0.0f) ? 0.0f : best);
            float r = geo.circumradiusOf(panels[i]);
            float nd = c - r;
            float fd = c + r;

            if (nd < 0.0f) nd = 0.0f;

            int nc = (int)(nd / maxFar * (float)resolution + 0.5f);
            int fc = (int)(fd / maxFar * (float)resolution + 0.5f);

            if (nc < 0)         nc = 0;

            if (nc > (int)maxC) nc = maxC;

            if (fc < 0)         fc = 0;

            if (fc > (int)maxC) fc = maxC;

            if (reverse) {
                // Invert and swap so near ≤ far still holds after flipping the axis.
                nearOut[i] = (uint8_t)((int)maxC - fc);
                farOut[i]  = (uint8_t)((int)maxC - nc);
            } else {
                nearOut[i] = (uint8_t)nc;
                farOut[i]  = (uint8_t)fc;
            }
        }

        return maxC;
    }
}  // namespace Lightnet
