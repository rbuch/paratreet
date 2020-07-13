#ifndef _PARTITION_H_
#define _PARTITION_H_

#include <vector>

#include "Particle.h"
#include "Traverser.h"
#include "ParticleMsg.h"
#include "NodeWrapper.h"
#include "paratreet.decl.h"

extern CProxy_TreeSpec treespec;
extern CProxy_Reader readers;

template <typename Data>
struct Partition : public CBase_Partition<Data> {
  std::vector<Particle> particles;
  std::vector<Node<Data>*> leaves;

  Traverser<Data> *traverser;
  int n_partitions;

  // filled in during traversal
  std::vector<std::vector<Node<Data>*>> interactions;

  CProxy_TreeCanopy<Data> tc_proxy;
  CProxy_CacheManager<Data> cm_proxy;
  CacheManager<Data> *cm_local;
  CProxy_Resumer<Data> r_proxy;
  Resumer<Data>* r_local;

  Partition(int, CProxy_CacheManager<Data>, CProxy_Resumer<Data>, TCHolder<Data>);

  template<typename Visitor> void startDown();
  void goDown(Key);
  void interact(const CkCallback& cb);

  void receive_leaves(std::vector<NodeWrapper<Data>>, int);
  void receive(ParticleMsg*);
  void destroy();
  void reset();
  void perturb(Real, bool);
  void flush(CProxy_Reader);
  void output(CProxy_Writer w, CkCallback cb);
};

template <typename Data>
Partition<Data>::Partition(
  int np, CProxy_CacheManager<Data> cm,
  CProxy_Resumer<Data> rp, TCHolder<Data> tc_holder
  )
{
  n_partitions = np;
  tc_proxy = tc_holder.proxy;
  r_proxy = rp;
  r_local = r_proxy.ckLocalBranch();
  r_local->part_proxy = this->thisProxy;
  r_local->resume_nodes_per_part.resize(n_partitions);
  r_local = r_proxy.ckLocalBranch();
  cm_proxy = cm;
  cm_local = cm_proxy.ckLocalBranch();
  r_local->cm_local = cm_local;
  cm_local->r_proxy = r_proxy;
}

template <typename Data>
template <typename Visitor>
void Partition<Data>::startDown()
{
  interactions.resize(leaves.size());
  traverser = new DownTraverser<Data, Visitor>(*this);
  traverser->start();
}

template <typename Data>
void Partition<Data>::goDown(Key new_key)
{
  traverser->resumeTrav(new_key);
}

template <typename Data>
void Partition<Data>::interact(const CkCallback& cb)
{
  traverser->interact();
  this->contribute(cb);
}

template <typename Data>
void Partition<Data>::receive_leaves(
  std::vector<NodeWrapper<Data>> data, int subtree_idx
  )
{
  int from = 0;
  for (const NodeWrapper<Data>& leaf : data) {
    Key k = Utility::removeLeadingZeros(leaf.key);
    from = Utility::binarySearchGE(k, &particles[0], from, particles.size());
    int to = Utility::binarySearchGE(k, &particles[0], from + 1, particles.size());
    CkAssert(leaf.n_particles == to - from);
    // ^ Not necessarily true for SFC for "shared" nodes?
    Node<Data> *node = treespec.ckLocalBranch()->template makeNode<Data>(
      leaf.key, leaf.depth, leaf.n_particles, &particles[from],
      subtree_idx, subtree_idx, leaf.is_leaf, nullptr, subtree_idx
      );
    node->type = Node<Data>::Type::Leaf;
    leaves.push_back(node);
  }
}

template <typename Data>
void Partition<Data>::receive(ParticleMsg *msg)
{
  particles.insert(particles.end(),
                   msg->particles, msg->particles + msg->n_particles);
  delete msg;
  std::sort(particles.begin(), particles.end());
}

template <typename Data>
void Partition<Data>::destroy()
{
  this->thisProxy[this->thisIndex].ckDestroy();
}

template <typename Data>
void Partition<Data>::reset()
{
  leaves.clear();
  interactions.clear();
}

template <typename Data>
void Partition<Data>::perturb(Real timestep, bool if_flush)
{
  // TODO: how do we decide when to send particles to other partitions?
  for (auto& p : particles)
    p.perturb(timestep, readers.ckLocalBranch()->universe.box);
  if (if_flush)
    flush(readers);
}

template <typename Data>
void Partition<Data>::flush(CProxy_Reader readers)
{
  ParticleMsg *msg = new (particles.size()) ParticleMsg(
    particles.data(), particles.size()
    );
  readers[CkMyPe()].receive(msg);
  particles.clear();
}

template <typename Data>
void Partition<Data>::output(CProxy_Writer w, CkCallback cb)
{
  std::vector<Particle> particles = this->particles;

  std::sort(particles.begin(), particles.end(),
            [](const Particle& left, const Particle& right) {
              return left.order < right.order;
            });

  int n_total_particles = readers.ckLocalBranch()->universe.n_particles;
  int particles_per_writer = n_total_particles / CkNumPes();
  if (particles_per_writer * CkNumPes() != n_total_particles)
    ++particles_per_writer;

  int particle_idx = 0;
  while (particle_idx < particles.size()) {
    int writer_idx = particles[particle_idx].order / particles_per_writer;
    int first_particle = writer_idx * particles_per_writer;
    std::vector<Particle> writer_particles;

    while (
      particles[particle_idx].order < first_particle + particles_per_writer
      && particle_idx < particles.size()
      ) {
      writer_particles.push_back(particles[particle_idx]);
      ++particle_idx;
    }

    w[writer_idx].receive(writer_particles, cb);
  }

  if (this->thisIndex != n_partitions - 1)
    this->thisProxy[this->thisIndex + 1].output(w, cb);
}

#endif /* _PARTITION_H_ */
