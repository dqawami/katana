/** partitioned graph wrapper for vertexCut -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Contains the vertex cut functionality to be used in dGraph.
 *
 * @author Gurbinder Gill <gurbinder533@gmail.com>
 */

#ifndef _GALOIS_DIST_HGRAPHCUSTOMEDGCUT_H
#define _GALOIS_DIST_HGRAPHCUSTOMEDGCUT_H

#include "galois/graphs/DistributedGraph.h"
#include <sstream>

template<typename NodeTy, typename EdgeTy>
class hGraph_customEdgeCut : public hGraph<NodeTy, EdgeTy> {
  constexpr static const char* const GRNAME = "dGraph_customEdgeCut";

  public:
    typedef hGraph<NodeTy, EdgeTy> base_hGraph;
    /** Utilities for reading partitioned graphs. **/
    struct NodeInfo {
      NodeInfo() :
        local_id(0), global_id(0), owner_id(0) {
        }
      NodeInfo(size_t l, size_t g, size_t o) :
        local_id(l), global_id(g), owner_id(o) {
        }
      size_t local_id;
      size_t global_id;
      size_t owner_id;
    };


    std::vector<NodeInfo> localToGlobalMap_meta;
    std::vector<size_t> OwnerVec; // To store the ownerIDs of sorted according to the Global IDs.
    std::vector<std::pair<uint32_t, uint32_t>> hostNodes;

    std::vector<size_t> GlobalVec_ordered; //Global Id's sorted vector.
    // To send edges to different hosts: #Src #Dst
    std::vector<std::vector<uint64_t>> assigned_edges_perhost;
    std::vector<uint64_t> recv_assigned_edges;
    std::vector<uint64_t> assignedNodes;
    uint64_t num_total_edges_to_receive;
    uint64_t numOwned = 0;

    // GID = localToGlobalVector[LID]
    std::vector<uint64_t> localToGlobalVector;
    // LID = globalToLocalMap[GID]
    std::unordered_map<uint64_t, uint32_t> globalToLocalMap;
    //To read the custom vertexIDMap
    std::vector<int32_t> vertexIDMap;

    //EXPERIMENT
    std::unordered_map<uint64_t, uint32_t> GlobalVec_map;

    //XXX: initialize to ~0
    std::vector<uint64_t> numNodes_per_host;

    //XXX: Use EdgeTy to determine if need to load edge weights or not.
    using Host_edges_map_type = typename std::conditional<!std::is_void<EdgeTy>::value, std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, uint32_t>>> , std::unordered_map<uint64_t, std::vector<uint64_t>>>::type;
    Host_edges_map_type host_edges_map;
    //std::unordered_map<uint64_t, std::vector<uint64_t>> host_edges_map;
    std::vector<uint64_t> numEdges_per_host;
    std::vector<std::pair<uint64_t, uint64_t>> gid2host_withoutEdges;

    uint64_t globalOffset;
    uint32_t numNodes;
    bool isBipartite;
    uint64_t numEdges;

    // TODO this is broken
    unsigned getHostID(uint64_t gid) const {
      auto lid = G2L(gid);
      return OwnerVec[lid];
    }

    // TODO this is broken
    size_t getOwner_lid(size_t lid) const {
      return OwnerVec[lid];
    }

    bool isOwned(uint64_t gid) const {
        //assert(isLocal(gid));
        if(isLocal(gid) && (globalToLocalMap.at(gid) < numOwned)) {
          return true;
        }
        return false;
    }

  virtual bool isLocal(uint64_t gid) const {
    assert(gid < base_hGraph::numGlobalNodes);
    return (globalToLocalMap.find(gid) != globalToLocalMap.end());
  }

  virtual uint32_t G2L(uint64_t gid) const {
    assert(isLocal(gid));
    return globalToLocalMap.at(gid);
  }

  virtual uint64_t L2G(uint32_t lid) const {
    return localToGlobalVector[lid];
  }


    std::string getMetaFileName(const std::string & basename, unsigned hostID, unsigned num_hosts){
      std::string result = basename;
      result+= ".META.";
      result+=std::to_string(hostID);
      result+= ".OF.";
      result+=std::to_string(num_hosts);
      return result;
    }

    bool readMetaFile(const std::string& metaFileName, std::vector<NodeInfo>& localToGlobalMap_meta){
      std::ifstream meta_file(metaFileName, std::ifstream::binary);
      if (!meta_file.is_open()) {
        std::cerr << "Unable to open file " << metaFileName << "! Exiting!\n";
        return false;
      }
      size_t num_entries;
      meta_file.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
      std::cout << "Partition :: " << " Number of nodes :: " << num_entries << "\n";
      for (size_t i = 0; i < num_entries; ++i) {
        std::pair<size_t, size_t> entry;
        size_t owner;
        meta_file.read(reinterpret_cast<char*>(&entry.first), sizeof(entry.first));
        meta_file.read(reinterpret_cast<char*>(&entry.second), sizeof(entry.second));
        meta_file.read(reinterpret_cast<char*>(&owner), sizeof(owner));
        localToGlobalMap_meta.push_back(NodeInfo(entry.second, entry.first, owner));
      }
      return true;
    }

    std::string getPartitionFileName(const std::string & basename, unsigned hostID, unsigned num_hosts){
      std::string result = basename;
      result+= ".PART.";
      result+=std::to_string(hostID);
      result+= ".OF.";
      result+=std::to_string(num_hosts);
      return result;
    }


    /* Reading vertexIDMap binary file
    *  Assuming that vertexIDMap binary file contains int32_t entries.
    */
    bool readVertexIDMappingFile(const std::string& vertexIDMap_filename, std::vector<int32_t>&vertexIDMap, uint32_t num_entries_to_read, uint32_t startLoc){
      std::ifstream meta_file(vertexIDMap_filename, std::ifstream::binary);
      if (!meta_file.is_open()) {
        std::cerr << "Unable to open file " << vertexIDMap_filename << "! Exiting!\n";
        return false;
      }
      meta_file.seekg(startLoc, meta_file.beg);
      //TODO: mmap rather than reading.
      meta_file.read(reinterpret_cast<char*>(&vertexIDMap[0]), sizeof(int32_t)*(num_entries_to_read));
      std::cout << " Number of nodes read :: " << num_entries_to_read << "\n";
      return true;
    }

    /* Reading the whole vertexIDMap binary file
    *  Assuming that vertexIDMap binary file contains int32_t entries.
    */
    bool readVertexIDMappingFile(const std::string& vertexIDMap_filename, std::vector<int32_t>&vertexIDMap, uint32_t num_entries_to_read){
      std::ifstream meta_file(vertexIDMap_filename, std::ifstream::binary);
      if (!meta_file.is_open()) {
        std::cerr << "Unable to open file " << vertexIDMap_filename << "! Exiting!\n";
        return false;
      }
      meta_file.read(reinterpret_cast<char*>(&vertexIDMap[0]), sizeof(int32_t)*(num_entries_to_read));
      std::cout << " Number of nodes read :: " << num_entries_to_read << "\n";
      return true;
    }


    std::pair<uint32_t, uint32_t> nodes_by_host(uint32_t host) const {
      return std::make_pair<uint32_t, uint32_t>(~0,~0);
    }

    std::pair<uint64_t, uint64_t> nodes_by_host_G(uint32_t host) const {
      return std::make_pair<uint64_t, uint64_t>(~0,~0);
    }


    /**
     * Constructor for Vertex Cut
     */
    hGraph_customEdgeCut(const std::string& filename, 
               const std::string& partitionFolder,
               unsigned host, unsigned _numHosts, 
               std::vector<unsigned>& scalefactor, 
               const std::string& vertexIDMap_filename, 
               bool transpose = false, 
               uint32_t VCutThreshold = 100, 
               bool bipartite = false) : base_hGraph(host, _numHosts) {
      if (!scalefactor.empty()) {
        if (base_hGraph::id == 0) {
          std::cerr << "WARNING: scalefactor not supported for custom-cuts\n";
        }
        scalefactor.clear();
      }

      if(vertexIDMap_filename.empty()){
        if (base_hGraph::id == 0) {
          std::cerr << "WARNING: no vertexIDMap_filename provided for custom-cuts\n";
        }
        abort();
      }

      galois::runtime::reportParam("(NULL)", "CUSTOM EDGE CUT", "0");

      galois::StatTimer Tgraph_construct("TIME_GRAPH_CONSTRUCT", GRNAME);
      Tgraph_construct.start();
      galois::StatTimer Tgraph_construct_comm("TIME_GRAPH_CONSTRUCT_COMM",
          GRNAME);

      galois::graphs::OfflineGraph g(filename);
      isBipartite = bipartite;
      base_hGraph::numGlobalNodes = g.size();
      base_hGraph::numGlobalEdges = g.sizeEdges();
      std::cerr << "[" << base_hGraph::id << "] Total nodes : " << 
                          base_hGraph::numGlobalNodes << " , Total edges : " << 
                          base_hGraph::numGlobalEdges << "\n";
      base_hGraph::computeMasters(g, scalefactor, isBipartite);

      //Read the vertexIDMap_filename for masters.
      auto startLoc = base_hGraph::gid2host[base_hGraph::id].first;
      auto num_entries_to_read = (base_hGraph::gid2host[base_hGraph::id].second - base_hGraph::gid2host[base_hGraph::id].first);
      assert(num_entries_to_read > 0);
      vertexIDMap.resize(num_entries_to_read);
      readVertexIDMappingFile(vertexIDMap_filename, vertexIDMap, num_entries_to_read, startLoc);

      //std::cout << "VERTEXMAP size : " << vertexIDMap.size() << " : " << vertexIDMap[200] << "\n";

      // at this point gid2Host has pairs for how to split nodes among
      // hosts; pair has begin and end
      uint64_t nodeBegin = base_hGraph::gid2host[base_hGraph::id].first;
      typename galois::graphs::OfflineGraph::edge_iterator edgeBegin = 
        g.edge_begin(nodeBegin);

      uint64_t nodeEnd = base_hGraph::gid2host[base_hGraph::id].second;
      typename galois::graphs::OfflineGraph::edge_iterator edgeEnd = 
        g.edge_begin(nodeEnd);

      galois::Timer edgeInspectionTimer;
      edgeInspectionTimer.start();

      galois::graphs::MPIGraph<EdgeTy> mpiGraph;
      mpiGraph.loadPartialGraph(filename, nodeBegin, nodeEnd, *edgeBegin, 
                                *edgeEnd, base_hGraph::numGlobalNodes,
      base_hGraph::numGlobalEdges);

      mpiGraph.resetReadCounters();

      uint64_t numEdges_distribute = edgeEnd - edgeBegin; 
      std::cerr << "[" << base_hGraph::id << "] Total edges to distribute : " << 
                   numEdges_distribute << "\n";

      /********************************************
       * Assign edges to the hosts using heuristics
       * and send/recv from other hosts.
       * ******************************************/
      //print_string(" : assign_send_receive_edges started");

      std::vector<uint64_t> prefixSumOfEdges;
      assign_edges_phase1(g, mpiGraph, numEdges_distribute, vertexIDMap,
                          prefixSumOfEdges, base_hGraph::mirrorNodes,
                          edgeInspectionTimer);

      base_hGraph::numOwned = numOwned;
      base_hGraph::numNodesWithEdges = numNodes;

      if (base_hGraph::numOwned > 0) {
        base_hGraph::beginMaster =
          G2L(localToGlobalVector[0]) ; //base_hGraph::gid2host[base_hGraph::id].first);
      } else {
        base_hGraph::beginMaster = 0;
      }

      /******************************************
       * Allocate and construct the graph
       *****************************************/
      base_hGraph::graph.allocateFrom(numNodes, numEdges);
      base_hGraph::graph.constructNodes();

      auto& base_graph = base_hGraph::graph;
      galois::do_all(
        galois::iterate((uint32_t)0, numNodes),
        [&] (auto n) {
          base_graph.fixEndEdge(n, prefixSumOfEdges[n]);
        },
        galois::no_stats(),
        galois::loopname("EdgeLoading"));

      loadEdges(base_hGraph::graph, mpiGraph, numEdges_distribute);

      mpiGraph.resetAndFree();

      /*******************************************/

      galois::runtime::getHostBarrier().wait();

      if (transpose && (numNodes > 0)) {
        base_hGraph::graph.transpose();
        base_hGraph::transposed = true;
      } else {
        // else because transpose will find thread ranges for you

        galois::StatTimer Tthread_ranges("TIME_THREAD_RANGES", GRNAME);
        Tthread_ranges.start();
        base_hGraph::determine_thread_ranges(numNodes, prefixSumOfEdges);
        Tthread_ranges.stop();
      }

      base_hGraph::determine_thread_ranges_master();
      base_hGraph::determine_thread_ranges_with_edges();
      base_hGraph::initialize_specific_ranges();

      Tgraph_construct.stop();

      /*****************************************
       * Communication PreProcessing:
       * Exchange mirrors and master nodes among
       * hosts
       ****************************************/
      Tgraph_construct_comm.start();
      base_hGraph::setup_communication();
      Tgraph_construct_comm.stop();
    }

    template<typename GraphTy>
    void loadEdges(GraphTy& graph, galois::graphs::MPIGraph<EdgeTy>& mpiGraph,
                   uint64_t numEdges_distribute){
      if (base_hGraph::id == 0) {
        if (std::is_void<typename GraphTy::edge_data_type>::value) {
          fprintf(stderr, "Loading void edge-data while creating edges.\n");
        } else {
          fprintf(stderr, "Loading edge-data while creating edges.\n");
        }
      }

      galois::Timer timer;
      timer.start();
      mpiGraph.resetReadCounters();

      assigned_edges_perhost.resize(base_hGraph::numHosts);

      assign_load_send_edges(graph, mpiGraph, numEdges_distribute);

      std::atomic<uint64_t> edgesToReceive;
      edgesToReceive.store(num_total_edges_to_receive);

      galois::on_each(
        [&](unsigned tid, unsigned nthreads) {
          receive_edges(graph, edgesToReceive);
        });

      ++galois::runtime::evilPhase;

      timer.stop();
      galois::gPrint("[", base_hGraph::id, "] Edge loading time: ", timer.get_usec()/1000000.0f, 
          " seconds to read ", mpiGraph.getBytesRead(), " bytes (",
          mpiGraph.getBytesRead()/(float)timer.get_usec(), " MBPS)\n");
    }

    // TODO Function way too long; split into helper functions + calls
    // Just calculating the number of edges to send to other hosts
    void assign_edges_phase1(galois::graphs::OfflineGraph& g, 
                             galois::graphs::MPIGraph<EdgeTy>& mpiGraph, 
                             uint64_t numEdges_distribute, 
                             std::vector<int32_t>& vertexIDMap, 
                             std::vector<uint64_t>& prefixSumOfEdges, 
                             std::vector<std::vector<size_t>>& mirrorNodes,
                             galois::Timer& edgeInspectionTimer) {
      // Go over assigned nodes and distribute edges.
      std::vector<std::vector<uint64_t>> numOutgoingEdges(base_hGraph::numHosts);
      std::vector<galois::DynamicBitSet> hasIncomingEdge(base_hGraph::numHosts);
      std::vector<galois::GAccumulator<uint64_t>> 
          num_assigned_edges_perhost(base_hGraph::numHosts);
      std::vector<galois::GAccumulator<uint32_t>> 
          num_assigned_nodes_perhost(base_hGraph::numHosts);
      num_total_edges_to_receive = 0;

      auto numNodesAssigned = base_hGraph::gid2host[base_hGraph::id].second - 
                              base_hGraph::gid2host[base_hGraph::id].first;

      for (uint32_t i = 0; i < base_hGraph::numHosts; ++i) {
        numOutgoingEdges[i].assign(numNodesAssigned, 0);
        hasIncomingEdge[i].resize(base_hGraph::numGlobalNodes);
      }

      mpiGraph.resetReadCounters();

      uint64_t globalOffset = base_hGraph::gid2host[base_hGraph::id].first;

      auto& net = galois::runtime::getSystemNetworkInterface();
      galois::do_all(
        galois::iterate(base_hGraph::gid2host[base_hGraph::id].first,
                        base_hGraph::gid2host[base_hGraph::id].second),
        [&] (auto src) {
          auto ee = mpiGraph.edgeBegin(src);
          auto ee_end = mpiGraph.edgeEnd(src);
          auto num_edges = std::distance(ee, ee_end);
          auto h = this->find_hostID(src - globalOffset);
          assert(h < net.Num);
          /*
           * numOutgoingEdges starts at 1, let the receive side know that
           * src is owned by the host h. Therefore, to get the actual number
           * of edges we have to substract 1.
           */
          numOutgoingEdges[h][src - globalOffset] = 1;
          num_assigned_nodes_perhost[h] += 1;
          num_assigned_edges_perhost[h] += num_edges;
          numOutgoingEdges[h][src - globalOffset] += num_edges;

          for (; ee != ee_end; ++ee) {
            auto gdst = mpiGraph.edgeDestination(*ee);
            hasIncomingEdge[h].set(gdst);
          }
        },
        galois::no_stats(),
        galois::loopname("EdgeInspection"));

      // time should have been started outside of this loop
      edgeInspectionTimer.stop();

      galois::gPrint("[", base_hGraph::id, "] Edge inspection time: ",
                     edgeInspectionTimer.get_usec()/1000000.0f, " seconds to read ",
                     mpiGraph.getBytesRead(), " bytes (",
                     mpiGraph.getBytesRead()/(float)edgeInspectionTimer.get_usec(),
                     " MBPS)\n");

      uint64_t check_numEdges = 0;
      for (uint32_t h = 0; h < base_hGraph::numHosts; ++h) {
        check_numEdges += num_assigned_edges_perhost[h].reduce();
      }
      galois::gPrint("[", base_hGraph::id, "] check_numEdges done\n");

      assert(check_numEdges == numEdges_distribute);

      numOwned = num_assigned_nodes_perhost[base_hGraph::id].reduce();
      /****** Exchange numOutgoingEdges sets *********/
      //send and clear assigned_edges_perhost to receive from other hosts
      galois::gPrint("[", base_hGraph::id, "] Starting Send of the data\n");
      for (unsigned x = 0; x < net.Num; ++x) {
        if(x == base_hGraph::id) continue;
        galois::runtime::SendBuffer b;
        galois::runtime::gSerialize(b, num_assigned_nodes_perhost[x].reduce());
        galois::runtime::gSerialize(b, num_assigned_edges_perhost[x].reduce());
        galois::runtime::gSerialize(b, numOutgoingEdges[x]);
        galois::runtime::gSerialize(b, hasIncomingEdge[x]);
        net.sendTagged(x, galois::runtime::evilPhase, b);
      }

      net.flush();
      galois::gPrint("[", base_hGraph::id, "] Sent the data\n");

      //receive
      for (unsigned x = 0; x < net.Num; ++x) {
        if(x == base_hGraph::id) continue;

        decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
        do {
          p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
        } while(!p);

        uint32_t num_nodes_from_host = 0;
        uint64_t num_edges_from_host = 0;
        galois::runtime::gDeserialize(p->second, num_nodes_from_host);
        galois::runtime::gDeserialize(p->second, num_edges_from_host);
        galois::runtime::gDeserialize(p->second, numOutgoingEdges[p->first]);
        galois::runtime::gDeserialize(p->second, hasIncomingEdge[p->first]);
        num_total_edges_to_receive += num_edges_from_host;
        numOwned += num_nodes_from_host;
      }
      galois::gPrint("[", base_hGraph::id, "] Received the data\n");
      ++galois::runtime::evilPhase;

      for (unsigned x = 0; x < net.Num; ++x) {
        if(x == base_hGraph::id) continue;
        assert(hasIncomingEdge[base_hGraph::id].size() == hasIncomingEdge[x].size());
        hasIncomingEdge[base_hGraph::id].bitwise_or(hasIncomingEdge[x]);
      }

      galois::gPrint("[", base_hGraph::id, "] Start: Fill local and global vectors\n");
      numNodes = 0;
      numEdges = 0;
      localToGlobalVector.reserve(numOwned);
      globalToLocalMap.reserve(numOwned);
      uint64_t src = 0;
      for(uint32_t i = 0; i < base_hGraph::numHosts; ++i){
        for(unsigned j = 0; j < numOutgoingEdges[i].size(); ++j){
          if(numOutgoingEdges[i][j] > 0){
            /* Subtract 1, since we added 1 before sending to know the
             * existence of nodes which do not have outgoing edges but
             * are still owned.
             */
            numEdges += (numOutgoingEdges[i][j] - 1);
            localToGlobalVector.push_back(src);
            globalToLocalMap[src] = numNodes++;
            prefixSumOfEdges.push_back(numEdges);
          }
          ++src;
        }
      }
      galois::gPrint("[", base_hGraph::id, "] End: Fill local and global vectors\n");

      /* At this point numNodes should be equal to the number of
       * nodes owned by the host.
       */
      assert(numNodes == numOwned);
      assert(localToGlobalVector.size() == numOwned);

      galois::gPrint("[", base_hGraph::id, "] Start: Fill Ghosts\n");
      /* In a separate loop for ghosts, so that all the masters can be assigned
       * contigous local ids.
       */
      for(uint64_t i = 0; i < base_hGraph::numGlobalNodes; ++i){
        /*
         * if it has incoming edges on this host, then it is a ghosts node.
         * Node should be created for this locally.
         * NOTE: Since this is edge cut, this ghost will not have outgoing edges,
         * therefore, will not add to the prefixSumOfEdges.
         */
        if (hasIncomingEdge[base_hGraph::id].test(i) && !isOwned(i)){
          localToGlobalVector.push_back(i);
          globalToLocalMap[i] = numNodes++;
          prefixSumOfEdges.push_back(numEdges);
        }
      }

      galois::gPrint("[", base_hGraph::id, "] End: Fill Ghosts\n");
      //std::cout << base_hGraph::id <<  "] numNodes : " << numNodes << ", numOwned : " << numOwned << ", numEdges : " << numEdges  <<"\n";

      uint32_t numGhosts = (localToGlobalVector.size() - numOwned);
      std::vector<uint32_t> mirror_mapping_to_hosts;
      if(numGhosts > 0){
        mirror_mapping_to_hosts.resize(numGhosts);
      }

      galois::gPrint("[", base_hGraph::id, "] Start: assignedNodes send\n");

      /****** Exchange assignedNodes: All to all *********/
      for (unsigned x = 0; x < net.Num; ++x) {
        if(x == base_hGraph::id) continue;
        galois::runtime::SendBuffer b;
#if 0
        galois::runtime::gSerialize(b, numOwned);
        for(uint32_t i = 0; i < numOwned; ++i) {
          if(base_hGraph::id == 0)
            std::cerr << localToGlobalVector[i] << "\n";
          galois::runtime::gSerialize(b,localToGlobalVector[i]);
        }
#endif
        std::vector<uint64_t> temp_vec(localToGlobalVector.begin(), localToGlobalVector.begin() + numOwned);
        galois::runtime::gSerialize(b, temp_vec);
        net.sendTagged(x, galois::runtime::evilPhase, b);
      }
      galois::gPrint("[", base_hGraph::id, "] Start: assignedNodes receive\n");

      galois::gPrint("[", base_hGraph::id, "] End: assignedNodes send\n");
      net.flush();

      galois::gPrint("[", base_hGraph::id, "] Start: assignedNodes receive\n");
      //receive
      for (unsigned x = 0; x < net.Num; ++x) {
        if(x == base_hGraph::id) continue;

        decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
        do {
          p = net.recieveTagged(galois::runtime::evilPhase, nullptr);
        } while(!p);

        std::vector<uint64_t> temp_vec;
        galois::runtime::gDeserialize(p->second, temp_vec);

        /*
         * This vector should be sorted. find_hostID expects a sorted vector.
         */
        assert(std::is_sorted(temp_vec.begin(), temp_vec.end()));


        uint32_t from_hostID = p->first;

        //update mirror to hosts mapping.
        galois::do_all(
            galois::iterate(localToGlobalVector.begin() + numOwned,
                            localToGlobalVector.end()),
          [&] (auto src) {
              auto h = this->find_hostID(temp_vec, src, from_hostID);
              if(h < std::numeric_limits<uint32_t>::max()){
                mirror_mapping_to_hosts[this->G2L(src) - numOwned] = h;
              }
          },
          galois::no_stats(),
          galois::loopname("MirrorToHostAssignment"));
      }
      galois::gPrint("[", base_hGraph::id, "] End: assignedNodes receive\n");
      ++galois::runtime::evilPhase;

      //fill mirror nodes
      for(uint32_t i = 0; i < (localToGlobalVector.size() - numOwned); ++i){
        mirrorNodes[mirror_mapping_to_hosts[i]].push_back(localToGlobalVector[numOwned + i]);
      }

      fprintf(stderr, "[%u] Resident nodes : %u , Resident edges : %lu\n", base_hGraph::id, numNodes, numEdges);
    }

    // Helper functions
    uint32_t find_hostID(uint64_t offset) {
      assert(offset < vertexIDMap.size());
      return vertexIDMap[offset];
      return std::numeric_limits<uint32_t>::max();
    }

    uint32_t find_hostID(std::vector<uint64_t>& vec, uint64_t gid, 
                         uint32_t from_hostID) {
      auto iter = std::lower_bound(vec.begin(), vec.end(), gid);
      if((*iter == gid) && (iter != vec.end())){
        return from_hostID;
      }
      return std::numeric_limits<uint32_t>::max();
    }

    // Edge type is not void.
    template<typename GraphTy, 
             typename std::enable_if<
               !std::is_void<typename GraphTy::edge_data_type>::value
             >::type* = nullptr>
      void assign_load_send_edges(GraphTy& graph, 
                                  galois::graphs::MPIGraph<EdgeTy>& mpiGraph, 
                                  uint64_t numEdges_distribute) {
        using DstVecType = std::vector<std::vector<uint64_t>>;
        galois::substrate::PerThreadStorage<DstVecType> 
            gdst_vecs(base_hGraph::numHosts);

        using DataVecType = 
            std::vector<std::vector<typename GraphTy::edge_data_type>>;
        galois::substrate::PerThreadStorage<DataVecType> 
            gdata_vecs(base_hGraph::numHosts);

        using SendBufferVecTy = std::vector<galois::runtime::SendBuffer>; 
        galois::substrate::PerThreadStorage<SendBufferVecTy> 
          sendBuffers(base_hGraph::numHosts);

        auto& net = galois::runtime::getSystemNetworkInterface();
        uint64_t globalOffset = base_hGraph::gid2host[base_hGraph::id].first;

        const unsigned& id = this->base_hGraph::id;
        const unsigned& numHosts = this->base_hGraph::numHosts;

        // Go over assigned nodes and distribute edges.
        galois::do_all(
          galois::iterate(base_hGraph::gid2host[base_hGraph::id].first,
                          base_hGraph::gid2host[base_hGraph::id].second),
          [&] (auto src) {
            auto ee = mpiGraph.edgeBegin(src);
            auto ee_end = mpiGraph.edgeEnd(src);

            auto& gdst_vec = *gdst_vecs.getLocal();
            auto& gdata_vec = *gdata_vecs.getLocal();

            for (unsigned i = 0; i < numHosts; ++i) {
              gdst_vec[i].clear();
              gdata_vec[i].clear();
              //gdst_vec[i].reserve(std::distance(ii, ee));
            }

            auto h = this->find_hostID(src - globalOffset);
            if (h != id) {
              // Assign edges for high degree nodes to the destination
              for(; ee != ee_end; ++ee){
                auto gdst = mpiGraph.edgeDestination(*ee);
                auto gdata = mpiGraph.edgeData(*ee);
                gdst_vec[h].push_back(gdst);
                gdata_vec[h].push_back(gdata);
              }
            } else {
              /*
               * If source is owned, all outgoing edges belong to this host
               */
              assert(this->isOwned(src));
              uint32_t lsrc = 0;
              uint64_t cur = 0;
              lsrc = this->G2L(src);
              cur = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
              //keep all edges with the source node
              for(; ee != ee_end; ++ee){
                auto gdst = mpiGraph.edgeDestination(*ee);
                uint32_t ldst = this->G2L(gdst);
                auto gdata = mpiGraph.edgeData(*ee);
                graph.constructEdge(cur++, ldst, gdata);
              }
              assert(cur == (*graph.edge_end(lsrc)));
            }

            // send 
            for (uint32_t h = 0; h < numHosts; ++h) {
              if (h == id) continue;
              if (gdst_vec[h].size()) {
                auto& sendBuffer = (*sendBuffers.getLocal())[h];
                galois::runtime::gSerialize(sendBuffer, src, gdst_vec[h], 
                                            gdata_vec[h]);
                if (sendBuffer.size() > partition_edge_send_buffer_size) {
                  net.sendTagged(h, galois::runtime::evilPhase, sendBuffer);
                  sendBuffer.getVec().clear();
                }
              }
            }
          },
          galois::no_stats(),
          galois::loopname("EdgeLoading"));

        // flush buffers
        for (unsigned threadNum = 0; 
             threadNum < sendBuffers.size(); 
             ++threadNum) {
          auto& sbr = *sendBuffers.getRemote(threadNum);
          for (unsigned h = 0; h < this->base_hGraph::numHosts; ++h) {
            if (h == this->base_hGraph::id) continue;
            auto& sendBuffer = sbr[h];
            if (sendBuffer.size() > 0) {
              net.sendTagged(h, galois::runtime::evilPhase, sendBuffer);
              sendBuffer.getVec().clear();
            }
          }
        }

        net.flush();
      }

    // Edge type is void.
    template<typename GraphTy, 
             typename std::enable_if<
               std::is_void<typename GraphTy::edge_data_type>::value
             >::type* = nullptr>
      void assign_load_send_edges(GraphTy& graph, 
                                  galois::graphs::MPIGraph<EdgeTy>& mpiGraph, 
                                  uint64_t numEdges_distribute) {
        using DstVecType = std::vector<std::vector<uint64_t>>;
        galois::substrate::PerThreadStorage<DstVecType> 
            gdst_vecs(base_hGraph::numHosts);

        using SendBufferVecTy = std::vector<galois::runtime::SendBuffer>; 
        galois::substrate::PerThreadStorage<SendBufferVecTy> 
          sendBuffers(base_hGraph::numHosts);

        auto& net = galois::runtime::getSystemNetworkInterface();
        uint64_t globalOffset = base_hGraph::gid2host[base_hGraph::id].first;

        const unsigned& id = this->base_hGraph::id;
        const unsigned& numHosts = this->base_hGraph::numHosts;

        // Go over assigned nodes and distribute edges.
        galois::do_all(
          galois::iterate(base_hGraph::gid2host[base_hGraph::id].first,
                          base_hGraph::gid2host[base_hGraph::id].second),
          [&] (auto src) {
            auto ee = mpiGraph.edgeBegin(src);
            auto ee_end = mpiGraph.edgeEnd(src);

            auto& gdst_vec = *gdst_vecs.getLocal();

            for (unsigned i = 0; i < numHosts; ++i) {
              gdst_vec[i].clear();
            }

            auto h = this->find_hostID(src - globalOffset);
            if (h != id) {
              //Assign edges for high degree nodes to the destination
              for (; ee != ee_end; ++ee) {
                auto gdst = mpiGraph.edgeDestination(*ee);
                gdst_vec[h].push_back(gdst);
              }
            } else {
              /*
               * If source is owned, all outgoing edges belong to this host
               */
              assert(this->isOwned(src));
              uint32_t lsrc = 0;
              uint64_t cur = 0;
              lsrc = this->G2L(src);
              cur = *graph.edge_begin(lsrc, galois::MethodFlag::UNPROTECTED);
              //keep all edges with the source node
              for(; ee != ee_end; ++ee){
                auto gdst = mpiGraph.edgeDestination(*ee);
                uint32_t ldst = this->G2L(gdst);
                graph.constructEdge(cur++, ldst);
              }
              assert(cur == (*graph.edge_end(lsrc)));
            }

            // send 
            for (uint32_t h = 0; h < numHosts; ++h) {
              if (h == id) continue;
              if (gdst_vec[h].size()) {
                auto& sendBuffer = (*sendBuffers.getLocal())[h];
                galois::runtime::gSerialize(sendBuffer, src, gdst_vec[h]);
                if (sendBuffer.size() > partition_edge_send_buffer_size) {
                  net.sendTagged(h, galois::runtime::evilPhase, sendBuffer);
                  sendBuffer.getVec().clear();
                }
              }
            }
          },
          galois::no_stats(),
          galois::loopname("EdgeLoading"));

        // flush buffers
        for (unsigned threadNum = 0; 
             threadNum < sendBuffers.size(); 
             ++threadNum) {
          auto& sbr = *sendBuffers.getRemote(threadNum);
          for (unsigned h = 0; h < this->base_hGraph::numHosts; ++h) {
            if (h == this->base_hGraph::id) continue;
            auto& sendBuffer = sbr[h];
            if (sendBuffer.size() > 0) {
              net.sendTagged(h, galois::runtime::evilPhase, sendBuffer);
              sendBuffer.getVec().clear();
            }
          }
        }

        net.flush();
      }


    template<typename GraphTy>
    void receive_edges(GraphTy& graph, std::atomic<uint64_t>& edgesToReceive) {
      galois::StatTimer StatTimer_exchange_edges("RECEIVE_EDGES_TIME", GRNAME);
      auto& net = galois::runtime::getSystemNetworkInterface();

      // receive the edges from other hosts
      while (edgesToReceive) {
        decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) p;
        p = net.recieveTagged(galois::runtime::evilPhase, nullptr);

        if (p) {
          auto& receiveBuffer = p->second;

          while (receiveBuffer.r_size() > 0) {
            uint64_t _src;
            std::vector<uint64_t> _gdst_vec;
            galois::runtime::gDeserialize(receiveBuffer, _src, _gdst_vec);
            edgesToReceive -= _gdst_vec.size();
            assert(isOwned(_src));
            uint32_t lsrc = G2L(_src);
            uint64_t cur = *graph.edge_begin(lsrc, 
                                             galois::MethodFlag::UNPROTECTED);
            uint64_t cur_end = *graph.edge_end(lsrc);
            assert((cur_end - cur) == _gdst_vec.size());

            deserializeEdges(graph, receiveBuffer, _gdst_vec, cur, cur_end);
          }
        }
      }
    }

  template<typename GraphTy,
           typename std::enable_if<
             !std::is_void<typename GraphTy::edge_data_type>::value
           >::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b, 
      std::vector<uint64_t>& gdst_vec, uint64_t& cur, uint64_t& cur_end) {
    std::vector<typename GraphTy::edge_data_type> gdata_vec;
    galois::runtime::gDeserialize(b, gdata_vec);
    uint64_t i = 0;
    while (cur < cur_end) {
      auto gdata = gdata_vec[i];
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst, gdata);
    }
  }

  template<typename GraphTy,
           typename std::enable_if<
             std::is_void<typename GraphTy::edge_data_type>::value
           >::type* = nullptr>
  void deserializeEdges(GraphTy& graph, galois::runtime::RecvBuffer& b, 
      std::vector<uint64_t>& gdst_vec, uint64_t& cur, uint64_t& cur_end) {
    uint64_t i = 0;
    while (cur < cur_end) {
      uint64_t gdst = gdst_vec[i++];
      uint32_t ldst = G2L(gdst);
      graph.constructEdge(cur++, ldst);
    }
  }

  /**
   * Returns the total nodes : master + slaves created on the local host.
   */
  uint64_t get_local_total_nodes() const {
    return (base_hGraph::numOwned);
  }

  void reset_bitset(typename base_hGraph::SyncType syncType, void (*bitset_reset_range)(size_t, size_t)) const {
    size_t first_owned = 0;
    size_t last_owned = 0;

    if (base_hGraph::numOwned > 0) {
      first_owned = G2L(localToGlobalVector[0]);
      last_owned = G2L(localToGlobalVector[numOwned - 1]);
      assert(first_owned <= last_owned);
      assert((last_owned - first_owned + 1) == base_hGraph::numOwned);
    } 

    if (syncType == base_hGraph::syncBroadcast) { // reset masters
      // only reset if we actually own something
      if (base_hGraph::numOwned > 0)
        bitset_reset_range(first_owned, last_owned);
    } else { // reset mirrors
      assert(syncType == base_hGraph::syncReduce);

      if (base_hGraph::numOwned > 0) {
        if (first_owned > 0) {
          bitset_reset_range(0, first_owned - 1);
        }
        if (last_owned < (numNodes - 1)) {
          bitset_reset_range(last_owned + 1, numNodes - 1);
        }
      } else {
        // only time we care is if we have ghost nodes, i.e. 
        // numNodes is non-zero
        if (numNodes > 0) {
          bitset_reset_range(0, numNodes - 1);
        }
      }
    }
  }

  void print_string(std::string s) {
    std::stringstream ss_cout;
    ss_cout << base_hGraph::id << s << "\n";
    std::cerr << ss_cout.str();
  }

  bool is_vertex_cut() const{
    return false;
  }
};
#endif
