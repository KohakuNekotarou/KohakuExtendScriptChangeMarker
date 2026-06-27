//========================================================================================
//
//  KESCMCore.h
//
//  Shared ChangeMarker (KESCM) operations, callable from both the script provider and the
//  panel UI. The overlay engine and all of its file-local state live in KESCMScriptProvider.cpp;
//  these thin entry points let the panel widget observer drive the exact same behavior the
//  scripting methods do (Start = mark changes + arm peek, Clear = clear marks + disarm peek, etc.).
//
//========================================================================================

#ifndef __KESCMCore_h__
#define __KESCMCore_h__

#include "BaseType.h"		// ErrorCode, bool16
#include "PMString.h"

class IDataBase;

// Compare every page of targetDB against the same-index page of sourceDB and (re)build the
// change-mark overlay. outReport receives the same status string the scripting method returns.
ErrorCode	KESCMDoMarkChangesDoc(IDataBase* targetDB, IDataBase* sourceDB, PMString& outReport);

// Drop the whole overlay (and the cached old-version images) and redraw db.
void		KESCMDoClearMarks(IDataBase* db);

// Toggle whether the marks print (and stay visible on screen). faintFlag = print at ~25%.
void		KESCMDoSetPrintMarks(bool16 printFlag, bool16 faintFlag, IDataBase* db);

// Arm / disarm the middle-button peek of the old version (also drives the panel's ON/OFF state).
void		KESCMDoArmMousePeek(IDataBase* targetDB, IDataBase* sourceDB);
void		KESCMDoDisarmMousePeek(IDataBase* db);

// Panel state accessors. "Armed" == the Start button has run and Clear has not; the panel shows
// the Target/Source names and the ON icon while armed, and hides the names + shows OFF otherwise.
bool16		KESCMIsArmed();
IDataBase*	KESCMArmedTargetDB();
IDataBase*	KESCMArmedSourceDB();

#endif // __KESCMCore_h__
