/********** tell emacs we use -*- c++ -*- style comments *******************
 $Revision: 1.1 $  $Author: trey $  $Date: 2006-06-27 18:19:26 $
   
 @file    MaxPlanesLowerBoundExec.h
 @brief   No brief

 Copyright (c) 2006, Trey Smith. All rights reserved.

 Licensed under the Apache License, Version 2.0 (the "License"); you may
 not use this file except in compliance with the License.  You may
 obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 implied.  See the License for the specific language governing
 permissions and limitations under the License.

 ***************************************************************************/

#ifndef INCMaxPlanesLowerBoundExec_h
#define INCMaxPlanesLowerBoundExec_h

/**********************************************************************
 * INCLUDES
 **********************************************************************/

#include <iostream>

#include <string>
#include <vector>

#include "PomdpExec.h"
#include "MaxPlanesLowerBound.h"

/**********************************************************************
 * CLASSES
 **********************************************************************/

namespace zmdp {

struct MaxPlanesLowerBoundExec : public PomdpExec {
  MaxPlanesLowerBound* policy;

  MaxPlanesLowerBoundExec(void);

  // read in the pomdp model and the policy generated by zmdpSolve
  void init(const std::string& modelFileName,
	    bool useFastParser,
	    const std::string& policyFileName);

  // implement PomdpExec virtual methods
  void setToInitialBelief(void);
  int chooseAction(void);
  void advanceToNextBelief(int a, int o);

  // can use for finer control
  void setBelief(const belief_vector& b);
};

}; // namespace zmdp

#endif // INCMaxPlanesLowerBoundExec_h

/***************************************************************************
 * REVISION HISTORY:
 * $Log: not supported by cvs2svn $
 *
 ***************************************************************************/