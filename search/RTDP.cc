/********** tell emacs we use -*- c++ -*- style comments *******************
 $Revision: 1.3 $  $Author: trey $  $Date: 2006-02-10 20:14:33 $
   
 @file    RTDP.cc
 @brief   No brief

 Copyright (c) 2006, Trey Smith. All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:

 * The software may not be sold or incorporated into a commercial
   product without specific prior written permission.
 * The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 ***************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include <iostream>
#include <fstream>
#include <queue>

#include "zmdpCommonDefs.h"
#include "zmdpCommonTime.h"
#include "MatrixUtils.h"
#include "Pomdp.h"
#include "RTDP.h"

using namespace std;
using namespace sla;
using namespace MatrixUtils;

#define OBS_IS_ZERO_EPS (1e-10)

namespace zmdp {

RTDP::RTDP(AbstractBound* _initUpperBound) :
  problem(NULL),
  initUpperBound(_initUpperBound),
  boundsFile(NULL),
  initialized(false)
{}

void RTDP::init(void)
{
  // initUpperBound set in constructor
  initUpperBound->initialize();

  lookup = new MDPHash();
  root = getNode(problem->getInitialState());

  previousElapsedTime = secondsToTimeval(0.0);

  numStatesTouched = 0;
  numStatesExpanded = 0;
  numTrials = 0;
  numBackups = 0;

  if (NULL != boundsFile) {
    (*boundsFile) << "# wallclock time"
		  << ", lower bound (n/a)"
		  << ", upper bound"
		  << ", # states touched"
		  << ", # states expanded"
		  << ", # trials"
		  << ", # backups"
		  << endl;
    boundsFile->flush();
  }

  initialized = true;
}

void RTDP::planInit(const MDP* _problem)
{
  problem = _problem;
  initialized = false;
}

MDPNode* RTDP::getNode(const state_vector& s)
{
  string hs = hashable(s);
  MDPHash::iterator pr = lookup->find(hs);
  if (lookup->end() == pr) {
    // create a new fringe node
    MDPNode& cn = *(new MDPNode);
    cn.s = s;
    cn.isTerminal = problem->getIsTerminalState(s);
    if (cn.isTerminal) {
      cn.ubVal = 0;
    } else {
      cn.ubVal = initUpperBound->getValue(s);
    }
    (*lookup)[hs] = &cn;
    numStatesTouched++;
    return &cn;
  } else {
    // return existing node
    return pr->second;
  }
}

void RTDP::expand(MDPNode& cn)
{
  // set up successors for this fringe node (possibly creating new fringe nodes)
  outcome_prob_vector opv;
  state_vector sp;
  cn.Q.resize(problem->getNumActions());
  FOR (a, problem->getNumActions()) {
    MDPQEntry& Qa = cn.Q[a];
    Qa.immediateReward = problem->getReward(cn.s, a);
    problem->getOutcomeProbVector(opv, cn.s, a);
    Qa.outcomes.resize(opv.size());
    FOR (o, opv.size()) {
      double oprob = opv(o);
      if (oprob > OBS_IS_ZERO_EPS) {
	MDPEdge* e = new MDPEdge();
        Qa.outcomes[o] = e;
        e->obsProb = oprob;
        e->nextState = getNode(problem->getNextState(sp, cn.s, a, o));
      } else {
        Qa.outcomes[o] = NULL;
      }
    }
  }

  numStatesExpanded++;
}

void RTDP::updateInternal(MDPNode& cn, int* maxUBActionP)
{
  int maxUBAction = -1;
  double ubVal;
  double maxUBVal = -99e+20;
  FOR (a, cn.getNumActions()) {
    MDPQEntry& Qa = cn.Q[a];
    ubVal = 0;
    FOR (o, Qa.getNumOutcomes()) {
      MDPEdge* e = Qa.outcomes[o];
      if (NULL != e) {
	MDPNode& sn = *e->nextState;
	double oprob = e->obsProb;
	ubVal += oprob * sn.ubVal;
      }
    }
    ubVal = Qa.immediateReward + problem->getDiscount() * ubVal;
    Qa.ubVal = ubVal;

    if (ubVal > maxUBVal) {
      maxUBVal = ubVal;
      maxUBAction = a;
    }
  }
  cn.ubVal = std::min(cn.ubVal, maxUBVal);

  if (NULL != maxUBActionP) *maxUBActionP = maxUBAction;
}

void RTDP::update(MDPNode& cn, int* maxUBActionP)
{
  if (cn.isFringe()) {
    expand(cn);
  }
  updateInternal(cn, maxUBActionP);

  numBackups++;
}

void RTDP::trialRecurse(MDPNode& cn, double pTarget, int depth)
{
  // check for termination
  if (cn.isTerminal) {
#if USE_DEBUG_PRINT
    printf("trialRecurse: depth=%d ubVal=%g terminal node (terminating)\n",
	   depth, cn.ubVal);
#endif
    return;
  }

  // update to ensure cached values in cn.Q are correct
  int maxUBAction;
  update(cn, &maxUBAction);

  // simulate outcome
  double r = unit_rand();
  int simulatedOutcome = 0;
  MDPQEntry& Qbest = cn.Q[maxUBAction];
  FOR (o, Qbest.getNumOutcomes()) {
    MDPEdge* e = Qbest.outcomes[o];
    if (NULL != e) {
      r -= e->obsProb;
      if (r <= 0) {
	simulatedOutcome = o;
	break;
      }
    }
  }

#if USE_DEBUG_PRINT
  printf("  trialRecurse: depth=%d a=%d o=%d ubVal=%g\n",
	 depth, maxUBAction, simulatedOutcome, cn.ubVal);
  printf("  trialRecurse: s=%s\n", sparseRep(cn.s).c_str());
#endif

  // recurse to successor
  MDPNode& bestSuccessor = *cn.Q[maxUBAction].outcomes[simulatedOutcome]->nextState;
  double pNextTarget = pTarget / problem->getDiscount();
  trialRecurse(bestSuccessor, pNextTarget, depth+1);

  update(cn, NULL);
}

void RTDP::doTrial(MDPNode& cn, double pTarget)
{
#if USE_DEBUG_PRINT
  printf("-*- doTrial: trial %d\n", (numTrials+1));
#endif

  trialRecurse(cn, pTarget, 0);
  numTrials++;
}

bool RTDP::planFixedTime(const state_vector& currentBelief,
			 double maxTimeSeconds,
			 double minPrecision)
{
  boundsStartTime = getTime() - previousElapsedTime;

  if (!initialized) {
    boundsStartTime = getTime();
    init();
  }

  // disable this termination check for now
  //if (root->ubVal - root->lbVal < minPrecision) return true;
  doTrial(*root, minPrecision);

  previousElapsedTime = getTime() - boundsStartTime;

  if (NULL != boundsFile) {
    double elapsed = timevalToSeconds(getTime() - boundsStartTime);
    if (elapsed / lastPrintTime >= 1.01) {
      (*boundsFile) << timevalToSeconds(getTime() - boundsStartTime)
		    << " " << -1 // lower bound, n/a
		    << " " << root->ubVal // upper bound
		    << " " << numStatesTouched // # states touched
		    << " " << numStatesExpanded // # states expanded
		    << " " << numTrials
		    << " " << numBackups
		    << endl;
      boundsFile->flush();
      lastPrintTime = elapsed;
    }
  }

  return false;
}

// this implementation is not very efficient, but it is guaranteed not
// to modify the algorithm state, so it can safely be used for
// simulation testing in the middle of a run.
int RTDP::chooseAction(const state_vector& s)
{
  outcome_prob_vector opv;
  state_vector sp;
  double bestUB = -99e+20;
  int bestUBAction = -1;
  FOR (a, problem->getNumActions()) {
    problem->getOutcomeProbVector(opv, s, a);
    double sumUB = 0;
    FOR (o, opv.size()) {
      if (opv(o) > OBS_IS_ZERO_EPS) {
	ValueInterval intv = getValueAt(problem->getNextState(sp, s, a, o));
	sumUB += opv(o) * intv.u;
      }
    }
    sumUB = problem->getReward(s,a) + problem->getDiscount() * sumUB;
    if (sumUB > bestUB) {
      bestUB = sumUB;
      bestUBAction = a;
    }
  }

  return bestUBAction;
}

void RTDP::setBoundsFile(std::ostream* _boundsFile)
{
  boundsFile = _boundsFile;
}

ValueInterval RTDP::getValueAt(const state_vector& s) const
{
  typeof(lookup->begin()) pr = lookup->find(hashable(s));
  if (lookup->end() == pr) {
    return ValueInterval(-1, // bogus value, n/a
			 initUpperBound->getValue(s));
  } else {
    const MDPNode& cn = *pr->second;
    return ValueInterval(-1, // bogus value n/a
			 cn.ubVal);
  }
}

}; // namespace zmdp

/***************************************************************************
 * REVISION HISTORY:
 * $Log: not supported by cvs2svn $
 * Revision 1.2  2006/02/10 19:33:32  trey
 * chooseAction() now relies on upper bound as it should (since the lower bound is not even calculated in vanilla RTDP!
 *
 * Revision 1.1  2006/02/09 21:59:04  trey
 * initial check-in
 *
 *
 ***************************************************************************/