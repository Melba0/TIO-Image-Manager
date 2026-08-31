#include "Clustering.h"
#include <algorithm>
#include <cmath>
#include <queue>

std::string zeroPad(int value, int width) {
    std::string s = std::to_string(value);
    if ((int)s.size() < width) s = std::string(width - (int)s.size(), '0') + s;
    return s;
}

namespace {

// Cosine similarity between two L2-normalized vectors (dot product).
float cosineSim(const std::vector<float>& a, const std::vector<float>& b) {
    size_t n = std::min(a.size(), b.size());
    float s = 0.0f;
    for (size_t i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}

}  // namespace

std::vector<int> clusterDbscan(const std::vector<std::vector<float>>& embs,
                               float threshold, int min_pts) {
    const int n = (int)embs.size();
    std::vector<int> labels(n, -1);

    if (n == 0) return labels;
    min_pts = std::max(1, min_pts);

    // region query: indices of all points within the cosine threshold
    auto region = [&](int p) {
        std::vector<int> out;
        for (int q = 0; q < n; ++q) {
            if (q == p) continue;
            if (cosineSim(embs[p], embs[q]) >= threshold) out.push_back(q);
        }
        return out;
    };

    int cluster = 0;
    for (int i = 0; i < n; ++i) {
        if (labels[i] != -1) continue;

        auto neighbors = region(i);
        // Core-point test: at least (min_pts-1) other points in the neighborhood.
        if ((int)neighbors.size() < min_pts - 1) {
            labels[i] = -1;  // noise (with min_pts == 1 this never happens)
            continue;
        }

        // Expand the cluster (BFS over the neighbor graph).
        labels[i] = cluster;
        std::queue<int> q;
        for (int nb : neighbors) q.push(nb);
        while (!q.empty()) {
            int cur = q.front();
            q.pop();
            if (labels[cur] != -1) continue;
            labels[cur] = cluster;
            auto nn = region(cur);
            if ((int)nn.size() >= min_pts - 1) {
                for (int nb : nn) {
                    if (labels[nb] == -1) q.push(nb);
                }
            }
        }
        ++cluster;
    }
    return labels;
}
