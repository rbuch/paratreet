#ifndef PARATREET_GRAVITYVISITOR_H_
#define PARATREET_GRAVITYVISITOR_H_

#include "paratreet.decl.h"
#include "common.h"
#include "Space.h"
#include <cmath>

extern CProxy_Resumer<CentroidData> centroid_resumer;

class GravityVisitor {
public:
  static constexpr const bool CallSelfLeaf = true;

private:
  // note gconst = 1
  static constexpr Real theta = 0.7;
  static constexpr int  nMinParticleNode = 6;

  static void addGravity(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {
    for (int i = 0; i < target.n_particles; i++) {
      Vector3D<Real> diff = source.data.centroid - target.particles()[i].position;
      Real rsq = diff.lengthSquared();
      if (rsq != 0) {
        Vector3D<Real> accel = diff * (source.data.sum_mass / (rsq * sqrt(rsq)));
        target.applyAcceleration(i, accel);
      }
    }
  }

public:
  /// @brief We've hit a leaf: N^2 interactions between all particles
  /// in the target and node.
  static void leaf(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {
      // addGravity(source, target);
    for (int i = 0; i < target.n_particles; i++) {
      Vector3D<Real> accel(0.0);
      for (int j = 0; j < source.n_particles; j++) {
          Vector3D<Real> diff = source.particles()[j].position - target.particles()[i].position;
          Real rsq = diff.lengthSquared();
          if (rsq != 0) {
              accel += diff * (source.particles()[j].mass / (rsq * sqrt(rsq)));
          }
      }
      target.applyAcceleration(i, accel);
    }
  }

  static bool open(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {
    if (source.n_particles <= nMinParticleNode) return true;
    return Space::intersect(target.data.box, source.data.centroid, source.data.rsq);
  }

  static void node(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {
    if (source.n_particles > 0) {
      addGravity(source, target);
    }
  }

  static bool cell(const SpatialNode<CentroidData>& source, SpatialNode<CentroidData>& target) {
    // find the closest particle in target to source's center
    // check all eight corners of bounding box
    // return true if one corner is within total_volume of node
    // else return false

    const OrientedBox<Real> box = target.data.box;
    if (box.contains(source.data.centroid)) return true;

    for (int i = 0; i < 2; i++) {
      for (int j = 0; j < 2; j++) {
        for (int k = 0; k < 2; k++) {
          Vector3D<Real> corner;
          corner.x = (i) ? box.greater_corner.x : box.lesser_corner.x;
          corner.y = (j) ? box.greater_corner.y : box.lesser_corner.y;
          corner.z = (k) ? box.greater_corner.z : box.lesser_corner.z;
          Vector3D<Real> dr = source.data.centroid - corner;
          Real dsq = dr.lengthSquared();
          if (theta * dsq < source.data.rsq) {
            return true;
          } 
        }
      }
    }
    return false;
  }

};

#endif //PARATREET_GRAVITYVISITOR_H_
