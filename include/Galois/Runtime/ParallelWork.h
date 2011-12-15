/** Galois scheduler and runtime -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
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
 * @section Description
 *
 * Implementation of the Galois foreach iterator. Includes various 
 * specializations to operators to reduce runtime overhead.
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */
#ifndef GALOIS_RUNTIME_PARALLELWORK_H
#define GALOIS_RUNTIME_PARALLELWORK_H

#include <algorithm>

#include "Galois/TypeTraits.h"
#include "Galois/Mem.h"
#include "Galois/Runtime/Config.h"
#include "Galois/Runtime/Support.h"
#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/Threads.h"
#include "Galois/Runtime/PerCPU.h"
#include "Galois/Runtime/WorkList.h"
#include "Galois/Runtime/DebugWorkList.h"
#include "Galois/Runtime/Termination.h"
#include "Galois/Runtime/LoopHooks.h"

namespace GaloisRuntime {

class LoopStatistics {
  unsigned long conflicts;
  unsigned long iterations;
public:
  LoopStatistics() :conflicts(0), iterations(0) {}
  void inc_iterations() {
    ++iterations;
  }
  void inc_conflicts() {
    ++conflicts;
  }
  void report_stat(const char* loopname) const {
    reportStatSum("Conflicts", conflicts, loopname);
    reportStatSum("Iterations", iterations, loopname);
    reportStatAvg("ConflictsDistribution", conflicts, loopname);
    reportStatAvg("IterationsDistribution", iterations, loopname);
  }
};

template<typename Function>
struct Configurator {
  enum {
    CollectStats = !Galois::does_not_need_stats<Function>::value,
    NeedsBreak = Galois::needs_parallel_break<Function>::value,
    NeedsPush = !Galois::does_not_need_parallel_push<Function>::value,
    NeedsContext = !Galois::does_not_need_context<Function>::value,
    NeedsPIA = Galois::needs_per_iter_alloc<Function>::value
  };
};

template<class WorkListTy, class Function>
class ForEachWork {
  typedef typename WorkListTy::value_type value_type;
  typedef GaloisRuntime::WorkList::LevelStealing<GaloisRuntime::WorkList::FIFO<value_type>, value_type> AbortedListTy;
  
  struct tldTy {
    Galois::UserContext<value_type> facing;
    SimpleRuntimeContext cnx;
    LoopStatistics stat;
    TerminationDetection::tokenHolder* lterm;
  };

  WorkListTy global_wl;
  Function& f;
  const char* loopname;

  PerCPU<tldTy> tdata;
  TerminationDetection term;
  AbortedListTy aborted;
  cache_line_storage<volatile long> break_happened; //hit flag
  cache_line_storage<volatile long> abort_happened; //hit flag

  void finishIteration(bool aborting, value_type val, tldTy& tld) {
    if (aborting) {
      clearConflictLock();
      tld.cnx.cancel_iteration();
      tld.stat.inc_conflicts();
      __sync_synchronize();
      aborted.push(val);
      abort_happened.data = 1;
      //don't listen to breaks from aborted iterations
      tld.facing.__resetBreakHappened();
      //clear push buffer
      tld.facing.__getPushBuffer().clear();
     }

    if (Configurator<Function>::NeedsPush) {
      for (typename Galois::UserContext<value_type>::pushBufferTy::iterator
	     b = tld.facing.__getPushBuffer().begin(),
	     e = tld.facing.__getPushBuffer().end();
	   b != e; ++b)
	global_wl.push(*b);
      tld.facing.__getPushBuffer().clear();
    }
    if (Configurator<Function>::NeedsPIA)
      tld.facing.__resetAlloc();
    if (Configurator<Function>::NeedsBreak)
      if (tld.facing.__breakHappened())
        break_happened.data = 1;
    if (!aborting)
      tld.cnx.commit_iteration();
  }

  void doProcess(value_type val, tldTy& tld) {
    tld.stat.inc_iterations();
    tld.cnx.start_iteration();
    bool aborted = false;
    try {
      f(val, tld.facing);
    } catch (int a) {
      aborted = true;
    }
    finishIteration(aborted, val, tld);
  }

  template<bool isLeader>
  inline void drainAborted(tldTy& tld) {
    if (!isLeader) return;
    if (!abort_happened.data) return;
    tld.lterm->workHappened();
    abort_happened.data = 0;
    std::pair<bool, value_type> p = aborted.pop();
    while (p.first) {
      if (Configurator<Function>::NeedsBreak && break_happened.data) 
	return;
      doProcess(p.second, tld);
      p = aborted.pop();
    }
  }

public:
  ForEachWork(Function& _f, const char* _loopname)
    :f(_f), loopname(_loopname) {
    abort_happened.data = 0;
    break_happened.data = 0;
  }

  ~ForEachWork() {
    for (unsigned int i = 0; i < GaloisRuntime::getSystemThreadPool().getActiveThreads(); ++i)
      tdata.get(i).stat.report_stat(loopname);
    GaloisRuntime::statDone();
  }

  template<typename Iter, typename Filter>
  bool AddInitialWork(Iter b, Iter e, Filter fil) {
    for(; b != e; ++b)
      if (fil(*b))
	global_wl.push(*b);
  }

  template<bool isLeader>
  void go() {
    tldTy& tld = tdata.get();
    setThreadContext(&tld.cnx);
    tld.lterm = term.getLocalTokenHolder();

    do {
      std::pair<bool, value_type> p = global_wl.pop();
      if (p.first)
        tld.lterm->workHappened();
      while (p.first) {
        if (Configurator<Function>::NeedsBreak && break_happened.data)
	  goto leaveLoop;
        doProcess(p.second, tld);
	drainAborted<isLeader>(tld);
	p = global_wl.pop();
      }

      drainAborted<isLeader>(tld);
      if (Configurator<Function>::NeedsBreak && break_happened.data)
	goto leaveLoop;
      term.localTermination();
    } while (!term.globalTermination());
  leaveLoop:
    setThreadContext(0);
  }

  void operator()() {
    if (tdata.myEffectiveID() == 0)
      go<true>();
    else
      go<false>();
  }
};

template<typename T1, typename T2, typename T3>
class FillWork {
  public:
  T1 b;
  T1 e;
  T2& g;
  T3 f;
  int num;
  int dist;
  FillWork(T1& _b, T1& _e, T2& _g, T3& _f) :b(_b), e(_e), g(_g), f(_f) {
    int a = getSystemThreadPool().getActiveThreads();
    dist = std::distance(b,e);
    num = (dist + a - 1) / a; //round up
    //std::cout << dist << " " << num << "\n";
  }
  void operator()(void) {
    int id = ThreadPool::getMyID();
    T1 b2 = b;
    T1 e2 = b;
    //stay in bounds
    int A = std::min(num * id, dist);
    int B = std::min(num * (id + 1), dist);
    std::advance(b2, A);
    std::advance(e2, B);
    g.AddInitialWork(b2,e2,f);
  }
};

struct select_all {
  template<typename T>
  bool operator() (T v) { return true; }
};

template<typename WLTy, typename IterTy, typename Function, typename Filter>
void for_each_impl(IterTy b, IterTy e, Function f, Filter fil, const char* loopname) {

  typedef typename WLTy::template retype<typename std::iterator_traits<IterTy>::value_type>::WL aWLTy;

  ForEachWork<aWLTy, Function> GW(f, loopname);

  FillWork<IterTy, ForEachWork<aWLTy, Function>, Filter > fw2(b,e,GW,fil);

  runCMD w[3];
  w[0].work = config::ref(fw2);
  w[0].isParallel = true;
  w[0].barrierAfter = true;
  w[1].work = config::ref(GW);
  w[1].isParallel = true;
  w[1].barrierAfter = true;
  w[2].work = &runAllLoopExitHandlers;
  w[2].isParallel = false;
  w[2].barrierAfter = true;
  getSystemThreadPool().run(&w[0], &w[3]);
}

}

#endif
