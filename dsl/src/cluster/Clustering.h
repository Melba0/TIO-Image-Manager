#pragma once
#include <vector>
#include <string>

// DBSCAN-style clustering over L2-normalized feature vectors using a cosine
// similarity threshold (two points are neighbors when their dot product is
// >= `threshold`).  Returns one label per input embedding; -1 marks noise.
//
// With min_pts == 1 every point is a core point, so the result is effectively
// the connected components of the neighbor graph (each isolated point becomes
// its own cluster, and nothing is ever noise).
//
// The algorithm is deterministic for a fixed input order.
std::vector<int> clusterDbscan(const std::vector<std::vector<float>>& embs,
                               float threshold, int min_pts = 1);

// Format a zero-padded numeric suffix used in cluster ids (e.g. 1 -> "001").
std::string zeroPad(int value, int width);
