//========================================================================================
//
//  KESCMPeek.h
//
//  Middle-button "peek": while a modifier+middle is held, show the old version of the
//  spread under the cursor (or refresh/compare), and restore on release. Owns the peek
//  state (armed target/source DB, hold flags) and the event watcher / startup service.
//  Only KESCMBaseScreenOpacity is exposed here; arm/disarm/state live in KESCMCore.h.
//
//========================================================================================
#ifndef __KESCMPeek_h__
#define __KESCMPeek_h__

#include "PMReal.h"

// Base on-screen opacity of the always-visible marks, derived from the print settings
// (print ON + faint => ~0.3, otherwise 1.0). Used by the peek release path and by
// KESCMDoSetPrintMarks. Defined in KESCMPeek.cpp.
PMReal KESCMBaseScreenOpacity();

#endif // __KESCMPeek_h__
