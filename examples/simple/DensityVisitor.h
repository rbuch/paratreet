#ifndef PARATREET_DENSITYVISITOR_H_
#define PARATREET_DENSITYVISITOR_H_

#include "paratreet.decl.h"
#include "common.h"
#include "Space.h"
#include "NeighborListCollector.h"
#include <cmath>
#include <vector>
#include <queue>

extern CProxy_NeighborListCollector neighbor_list_collector;

struct DensityVisitor {
public:
  static constexpr const bool CallSelfLeaf = true;

// in leaf check for not same particle plz
private:
  static constexpr const int k = 32;

public:
  static bool open(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {
    // Check if any of the target balls intersect the source volume
    for (int i = 0; i < target.n_particles; i++) {
      if (target.data.neighbors[i].size() < k) return true;
      if(Space::intersect(source.data.box, target.particles()[i].position, target.data.neighbors[i].front().fKey))
        return true;
    }
    return false;
  }

  static void node(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {}

  static void leaf(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {
    auto nlc = neighbor_list_collector.ckLocalBranch();
    for (int i = 0; i < target.n_particles; i++) {
      auto& Q = target.data.neighbors[i];
      for (int j = 0; j < source.n_particles; j++) {
        const auto& sp = source.particles()[j]; //source particle
        Vector3D<Real> dr = target.particles()[i].position - sp.position;
        auto dsq = dr.lengthSquared();
        // Remove the most distant neighbor if this one is closer and the list is full
        if (Q.size() == k) {
          if (dsq < Q.front().fKey) { // closer than farthest away
            std::pop_heap(Q.begin(), Q.end());
            Q.pop_back();
          }
        }
        // Add the particle to the neighbor list if it isnt filled up
        if (Q.size() < k) {
          nlc->makeRequest(source.home_pe, sp.key);
          pqSmoothNode pqNew;
          pqNew.mass = sp.mass;
          pqNew.fKey = dsq;
          pqNew.pKey = sp.key;
          Q.push_back(pqNew);
          std::push_heap(Q.begin(), Q.end());
        }
      }
    }
  }
};

#endif // PARATREET_DENSITYVISITOR_H_
