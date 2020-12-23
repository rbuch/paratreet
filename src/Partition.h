#ifndef _PARTITION_H_
#define _PARTITION_H_

#include <vector>

#include "Particle.h"
#include "Traverser.h"
#include "ParticleMsg.h"
#include "NodeWrapper.h"
#include "paratreet.decl.h"

extern CProxy_TreeSpec treespec;
extern CProxy_TreeSpec treespec_subtrees;
extern CProxy_Reader readers;

template <typename Data>
struct Partition : public CBase_Partition<Data> {
  std::vector<Particle> particles, incoming_particles;
  std::vector<std::unique_ptr<Node<Data>>> leaves;

  std::unique_ptr<Traverser<Data>> traverser;
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
  template<typename Visitor> void startUpAndDown();
  void goDown(Key);
  void interact(const CkCallback& cb);

  void receiveLeaves(std::vector<NodeWrapper>, std::vector<Key>, int);
  void receive(ParticleMsg*);
  void destroy();
  void reset();
  void perturb(TPHolder<Data>, Real, bool);
  void flush(CProxy_Reader);
  void output(CProxy_Writer w, CkCallback cb);
  void initLocalBranches();
  void pup(PUP::er& p);
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
  cm_proxy = cm;
  initLocalBranches();
}

template <typename Data>
void Partition<Data>::initLocalBranches() {
  r_local = r_proxy.ckLocalBranch();
  r_local->part_proxy = this->thisProxy;
  r_local->resume_nodes_per_part.resize(n_partitions);
  cm_local = cm_proxy.ckLocalBranch();
  r_local->cm_local = cm_local;
  cm_local->r_proxy = r_proxy;
}

template <typename Data>
template <typename Visitor>
void Partition<Data>::startDown()
{
  initLocalBranches();
  interactions.resize(leaves.size());
  traverser.reset(new DownTraverser<Data, Visitor>(*this));
  traverser->start();
}

template <typename Data>
template <typename Visitor>
void Partition<Data>::startUpAndDown()
{
  initLocalBranches();
  interactions.resize(leaves.size());
  traverser.reset(new UpnDTraverser<Data, Visitor>(*this));
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
void Partition<Data>::receiveLeaves(
  std::vector<NodeWrapper> data, std::vector<Key> all_particle_keys, int subtree_idx)
{
  particles.reserve(incoming_particles.size());
  std::function<bool(const Particle&, Key)> compGE = [] (const Particle& a, Key b) {return a.key >= b;};
  std::sort(incoming_particles.begin(), incoming_particles.end());
  int received_part_index = particles.size();
  for (int i = 0; i < all_particle_keys.size(); i++) {
    int particle_idx = Utility::binarySearchComp(all_particle_keys[i],
      incoming_particles.data(), 0, incoming_particles.size(), compGE);
    if (particle_idx == incoming_particles.size()) CkAbort("couldnt find particle key");
    else particles.push_back(incoming_particles[particle_idx]);
  }
  for (const NodeWrapper& leaf : data) {
    Node<Data> *node = treespec.ckLocalBranch()->template makeNode<Data>(
      leaf.key, leaf.depth, leaf.n_particles, &particles[received_part_index],
      subtree_idx, subtree_idx, true, nullptr, subtree_idx
      );
    received_part_index += leaf.n_particles;
    node->type = Node<Data>::Type::Leaf;
    node->data = Data(node->particles(), node->n_particles);
    leaves.emplace_back(node);
  }
  cm_local->num_buckets += leaves.size();
}

template <typename Data>
void Partition<Data>::receive(ParticleMsg *msg)
{
  incoming_particles.insert(incoming_particles.end(),
                   msg->particles, msg->particles + msg->n_particles);
  delete msg;
}

template <typename Data>
void Partition<Data>::destroy()
{
  particles.clear();
  reset();
  this->thisProxy[this->thisIndex].ckDestroy();
}

template <typename Data>
void Partition<Data>::reset()
{
  incoming_particles = std::move(particles);
  traverser.reset();
  leaves.clear();
  interactions.clear();
}

template <typename Data>
void Partition<Data>::pup(PUP::er& p)
{
  p | particles;
  p | n_partitions;
  p | tc_proxy;
  p | cm_proxy;
  p | r_proxy;
  if (p.isUnpacking()) {
    initLocalBranches();
  }
}

template <typename Data>
void Partition<Data>::perturb(TPHolder<Data> tp_holder, Real timestep, bool if_flush)
{
  for (auto && p : particles) {
    p.perturb(timestep, readers.ckLocalBranch()->universe.box);
  }

  if (if_flush) {
    flush(readers);
  }
  else {
    auto sendParticles = [&](int dest, int n_particles, Particle* particles) {
      ParticleMsg* msg = new (n_particles) ParticleMsg(particles, n_particles);
      tp_holder.proxy[dest].receive(msg);
    };
    treespec_subtrees.ckLocalBranch()->getDecomposition()->flush(particles, sendParticles);
  }
}

template <typename Data>
void Partition<Data>::flush(CProxy_Reader readers)
{
  ParticleMsg *msg = new (particles.size()) ParticleMsg(
    particles.data(), particles.size()
    );
  readers[CkMyPe()].receive(msg);
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
