/**
 * @file HybridBehaviour.h
 *
 * A framework that supports lightweight stackless coroutine behaviours,
 * hierarchical state machine (HSM) behaviours, and reactive Behaviour Tree
 * combinators (sequence and fallback). Collectively we refer to 
 * coroutines (coros) and HSMs as tasks.
 *
 * Features:
 * - tasks look like resumable methods contained within some arbitrary class
 *   (No class hierarchy or base class type is imposed)
 * - tasks have access to all the members of the container class
 * - Implicit context management (auto-reset on task switch).
 * - Explicit separation of Inputs (ARGS) and State (VARS).
 * - Concise syntax with optional ARGS/VARS/DEFINES_PARAMS.
 * - Inline and out-of-class definition support via DECLARE/DEFINE.
 *
 * Acknowledgement:
 * This framework draws on ideas and some structural patterns from CABSL
 * (C-based Agent Behavior Specification Language) by Thomas Röfer, B-Human.
 * Specifically: the option-as-class-method architecture, the inline/out-of-class
 * split syntax, the args/vars/defs three-way parameter distinction, and the
 * goto-label-based HSM state entry mechanism are inspired by CABSL. 
 * The coroutine system, BT combinators, general Env/Context design
 * and Env/Context design are original additions.
 */

#pragma once

#include "HbsMacroUtils.h"
#include "HbsTypes.h"
#include "TickTrace.h"

#include <memory>
#include <new>
#include <format>

// configurable values and macros that can be overridden externally
#ifndef HBS_MAX_BT_SLOTS
#define HBS_MAX_BT_SLOTS 8
#endif

#ifndef HBS_ASSERT
#include <cassert>
#define HBS_ASSERT(cond_, ...) assert((cond_) __VA_OPT__(&& (__VA_ARGS__)))
#endif


namespace Hbs
{
template<class T>
T testAndSet(T& var, T value)
{
    T oldVal = var;
    var = value;
    return oldVal;
}

/**
 * Central environment for a group of coro or HSM tasks.
 * Tracks time and tick count for proper continuity checking and auto-reset.
 *
 * Usage:
 *   1. Declare in container class via HBS_ENV() macro
 *   2. Call HBS_BEGIN_TICK(currentTime) before executing tasks each frame
 */
class Env
{
public:
  // no default constructor
  explicit Env(TickTrace* trace = nullptr) : mTickTrace(trace) {}

  inline Time currentTickTime() const { return mCurrentTickTime; }
  inline Time prevTickTime() const { return mPrevTickTime; }
  inline unsigned currentTick() const { return mCurrentTick; }
  inline unsigned prevTick() const { return mPrevTick; }

  TickTrace* tickTrace() { return mTickTrace; }

  /**
   * Call at the start of each cognition cycle before executing any coroutines.
   * Updates time and tick count for continuity checking.
   */
  void beginTick(Time timeNow)
  {
    mPrevTickTime = mCurrentTickTime;
    mPrevTick = mCurrentTick;
    mCurrentTickTime = timeNow;
    mCurrentTick++;

    if (mTickTrace)
      mTickTrace->beginTick();
  }

private:
  Time mCurrentTickTime = 0;
  Time mPrevTickTime = 0;
  unsigned mCurrentTick = 0;
  unsigned mPrevTick = 0;

  TickTrace* mTickTrace;
};

/**
 * Context class used to remember state of a coroutine.
 * There is a context instance for each cororoutine, but this is hidden behind 
 * the macro implementations
 */
class Context
{
public:
  static constexpr int RESUME_RESET = -1;
  static constexpr int RESUME_START = 0;

  enum class CycleMode { NONE, TRY, REQUIRED };

  Context(Env& env, const char *name)
      : mEnv(env), mTaskName(name)
  {
  }

  Status lastStatus() const { return mLastStatus; }
  CycleMode cycleMode() const { return mCycleMode; }

  Time currentTickTime() { return mEnv.currentTickTime(); }
  unsigned currentTick() { return mEnv.currentTick(); }

  Time taskDuration() const { return mEnv.currentTickTime() - mTaskStartTime; }
  unsigned taskTicks() const { return mEnv.currentTick() - mTaskStartTick; }
  Time stateDuration() const { return mEnv.currentTickTime() - mStateStartTime; }
  unsigned stateTicks() const { return mEnv.currentTick() - mStateStartTick; }

  Time timeSince(Time before) { return mEnv.currentTickTime() - before; }
  unsigned ticksSince(unsigned before) { return mEnv.currentTick() - before; }

  const char* stateName() const { return mStateName; }
  const char* taskName() const { return mTaskName; }

  bool isRunning() const { return mLastStatus == RUNNING; }
  bool finished() const { return (mLastStatus == SUCCESS) || (mLastStatus == FAILURE); }
  bool succeeded() const { return mLastStatus == SUCCESS; }
  bool failed() const { return mLastStatus == FAILURE; }
  
  void reset()
  {
    mResumePoint = RESUME_RESET;
    mVarsNeedReset = true;
    mLastStatus = Status::INITIAL;
    mTaskStartTick = 0; // when this is 0 on beginTick, we reset the times also
    mStateStartTick = 0;

  }

  template <typename... Args> 
  void trace(std::format_string<Args...> fmt, Args &&...args)
  {
    auto *t = mEnv.tickTrace();
    if (t)
      t->addFrameString(mTraceFrameIndex, std::format(fmt, std::forward<Args>(args)...));
  }

  // ---------------------------------------------------------------------------
  // the following methods (named with a '_' suffix) are internal implementation 
  // exposed for macro use only - do not call directly!
  // ---------------------------------------------------------------------------

  int resumePoint_() const { return mResumePoint; }
  void setResumePoint_(int resumePoint) { mResumePoint = resumePoint; }

  bool isAtStart_() const { return mResumePoint == RESUME_START; }
  bool isAtState_(int line) const { return mResumePoint == line; }
  bool isStateEntry_() const { return stateTicks() == 0; }
  bool transitionsEnabled_() const { return !isStateEntry_() || mTransitionsOnEntry; }

  void setLastStatus_(Status status) { mLastStatus = status; }

  void setCycleMode_(CycleMode mode) { mCycleMode = mode; }

  bool holdIfPossible_()
  {
    if (!finished())
      return false;
    if (skippedTickAutoResetNeeded())
      return false;                       // gap: let normal reset happen on next real call
    mLastTick = mEnv.currentTick();       // keep alive to prevent gap-reset next tick
    return true;
  }

  // helper to begin a tick of the associated task
  void beginTick_()
  {
    if (skippedTickAutoResetNeeded())
    {
       reset();
    }
    else if (finished()) // handle cycling the terminal state
    {
      bool canCycle = (mResumePoint != RESUME_RESET);

      if (mCycleMode == CycleMode::REQUIRED)
        HBS_ASSERT(canCycle && "cycling requested but task not capable"); // fail loudly if requested cycling not possible

      if (!canCycle || (mCycleMode == CycleMode::NONE))
        reset(); // default: auto-reset; when cycle not possible for CycleMode::MAYBE and release-mode fallback
    }

    mCycleMode = CycleMode::NONE; // consume the cycle mode — must be re-set each tick by the wrapper

    // If reset happened or first run -- TODO check this logic
    if ((mResumePoint == RESUME_RESET) && (mLastTick != mEnv.currentTick()))
    {
      mTaskStartTime = mEnv.currentTickTime();
      mTaskStartTick = mEnv.currentTick();

      mStateStartTime = mEnv.currentTickTime();
      mStateStartTick = mEnv.currentTick();
      mStateName = "Start";
    }

    mLastTick = mEnv.currentTick();
  }

  // helper to add to the tick trace for the associated task
  void beginTrace_()
  {
    auto* t = mEnv.tickTrace();
    mTraceFrameIndex = t ? t->beginTaskFrame(mTaskName, taskDuration(), taskTicks()) : -1;
  }

  // helper to finalise the tick trace for the associated task
  void endTrace_()
  {
    auto *t = mEnv.tickTrace();
    if (t)
      t->endTaskFrame(mTraceFrameIndex, mLastStatus, mStateName, stateDuration(), stateTicks());
  }

  // mark a new "state" or stage of progress in a coro
  // can be safely and cheaply called on each tick (E.g. in a loop)
  void setCoroState_(const char* sname, int resumePoint)
  {
    // fast check for repeat of the current checkpoint
    if (resumePoint != mResumePoint)
    {
      mResumePoint = resumePoint;
      mStateName = sname;
      mStateStartTime = mEnv.currentTickTime();
      mStateStartTick = mEnv.currentTick();
    }
  }

  // mark the first entry into a state (usually after a transition)
  // note that values are only set if this is a new resumePoint (i.e. after changing state)
  void enterHsmState_(int resumePoint, const char *sname, Status status, bool transitionsOnEntry = false)
  {
    if (resumePoint != mResumePoint)
    {
      mResumePoint = resumePoint;
      mStateName = sname;
      mStateStartTime = mEnv.currentTickTime();
      mStateStartTick = mEnv.currentTick();

      // this will be the status after we have executed the state code
      mLastStatus = status;
      mTransitionsOnEntry = transitionsOnEntry;
    }
  }

  /** ensure the VarsType is ready for the current frame, reinitialising it if needed */
  template <typename VarsType>
  void prepareVars_()
  {
    if (!mVars)
    {
      mVars.reset(new VarsType());
      mVarsNeedReset = false;
    }
    else if (mVarsNeedReset)
    {
      // Reconstruct in place
      auto* ptr = static_cast<VarsType*>(mVars.get());
      ptr->~VarsType();
      new (ptr) VarsType();
      mVarsNeedReset = false;
    }
  }

  template <typename VarsType>
  VarsType* getVars_() { return static_cast<VarsType*>(mVars.get()); }

  // ----------------------------------------------------------------------
  // BT specific macro support
  // ----------------------------------------------------------------------

  // Called at the top of each BT combinator lambda.
  // Invalidates slots when the active combinator changes; always resets cursor.
  void beginBtCombinator_(uint16_t combId)
  {
    if (combId != mActiveBtCombId)
    {
      mActiveBtCombId = combId;
      mBtSlotCount = 0;
    }
    mBtCursor = 0;
  }

  // Backs HBS_HOLD and HBS_DEFER. Delegates all logic here to keep the macro minimal.
  // HBS_DEFER is HBS_HOLD with the additional step of returning RUNNING one more time
  // when the terminal status is first encountered to prevent immediate progression to the next
  // child in the combinator.
  // Note that fn is a lambda that has already captured the real function/task to be invokved and its args
  template <typename Fn> Status applyHold_(Fn &&fn, bool defer)
  {
    BtSlot& slot = nextBtSlot_();
    if (slot.heldOrPending)
      return slot.cached;
    Status s = fn();
    if (s != RUNNING)
    {
      slot.heldOrPending = true;
      slot.cached = s;
      if (defer)
        return RUNNING; // defer the terminal status for one tick
    }
    return s;
  }

private:
  int mResumePoint = RESUME_RESET;
  unsigned mLastTick = 0;
  Status mLastStatus = Status::INITIAL;
  bool mTransitionsOnEntry = false;
  CycleMode mCycleMode = CycleMode::NONE;

  Env &mEnv;
  
  Time mTaskStartTime = 0;
  Time mStateStartTime = 0;
  const char* mTaskName = "unknownTask";
  const char *mStateName = "start";
  unsigned int mTaskStartTick = 0;
  unsigned int mStateStartTick = 0;

  bool mVarsNeedReset = false;
  std::unique_ptr<VarStorage> mVars;

  int mTraceFrameIndex = -1;

  // implement our skipped tick semantics which should automatically reset a
  // task back to its initial condition
  bool skippedTickAutoResetNeeded()
  {
    return ((mLastTick != mEnv.currentTick() - 1) && (mLastTick != mEnv.currentTick()));
  }

  // ----------------------------------------------------------------------
  // BT specific support
  // ----------------------------------------------------------------------
private:
  struct BtSlot
  {
    Status cached = INITIAL;
    bool heldOrPending = false; // true if status held or terminal status being deferred to next tick
  };

  static constexpr int MAX_BT_SLOTS = HBS_MAX_BT_SLOTS;

  BtSlot mBtSlots[MAX_BT_SLOTS] = {};
  uint8_t mBtSlotCount = 0;     // high-water mark of allocated slots
  uint8_t mBtCursor = 0;        // position during current combinator evaluation
  uint16_t mActiveBtCombId = 0; // identity of currently active combinator

  BtSlot &nextBtSlot_()
  {
    if (mBtCursor == mBtSlotCount)
    {
      HBS_ASSERT(mBtSlotCount < MAX_BT_SLOTS);
      if (mBtSlotCount < MAX_BT_SLOTS)
        mBtSlots[mBtSlotCount++] = BtSlot{};
      else
        return mBtSlots[MAX_BT_SLOTS - 1]; // release mode only: reuse last slot - programming error if we get here and
                                         // likely breaks the sequence behaviour
    }
    return mBtSlots[mBtCursor++];
  }
};


// helper function
inline bool finished(Status s) { return s == Hbs::SUCCESS || s == Hbs::FAILURE; }


} // namespace Hbs

// ===========================================================================
// top level Macros
// ===========================================================================

/**
 * HBS_ENV: Macro to declare the standard Env member in a container class.
 *
 * Usage:
 * @code
 *   class MyBehaviour {
 *   public:
 *     HBS_ENV();
 *
 *     CORO_DEFINE((task1), ...) { ... }
 *     HSM_DEFINE((task2), ...) { ... }
 *
 *     void execute(Time currentTime) {
 *       HBS_BEGIN_TICK(currentTime);
 *       myTask();
 *     }
 *   };
 * @endcode
 *
 * This declares the shared env that all coroutines in this class will
 * reference. Call HBS_BEGIN_TICK(currentTime) at the start of each cognition
 * cycle.
 */
#define HBS_ENV(...) \
  Hbs::Env hbsEnv{__VA_ARGS__}

/**
 * HBS_BEGIN_TICK: Call at the start of each cognition cycle before executing
 * coroutines. Updates the shared environment's time and tick count.
 *
 * @param timeNow Current time value (Time)
 */
#define HBS_BEGIN_TICK(timeNow) \
  hbsEnv.beginTick(timeNow)

// --- Naming Convention ---
// HBS__ : Prefix for "Hbs" internal macros.
// M    : "Mapper" macro. The macro to apply to each item in a list.
// D    : "Delimiter". The token to inject between items (e.g., a comma).


// ===========================================================================
// Macros common to all tasks
// ===========================================================================

/// access the context object related functions of a task
#define TASK(methodName_) methodName_##_ctx 

/// own context
#define THIS_TASK ctx

/**
 * Wrapper/decorator to determine if a task should cycle (repeat) its terminal
 * state instead of the default auto reset after terminal status. 
 *
 * ASSERTs if the
 * task is not capable of cycling. A HSM with a SUCCESS/FAILURE state, can cycle
 * by default. A Coro requires a block like while (cond...) { code...;
 * CORO_YIELD_SUCCESS(); } or similar in order to cycle. CORO_SUCCESS/FAILURE or
 * CORO_END_SUCCESS/FAILURE do not support cycling.
 */
#define HBS_CYCLE(taskName_) \
    (taskName_##_ctx.setCycleMode_(Hbs::Context::CycleMode::REQUIRED), taskName_())

/**
 * Variant of HBS_CYCLE that tries to cycle but falls back to auto reset without
 * an assert if the task does not support cycling.
 */
#define HBS_TRY_CYCLE(taskName_) \
    (taskName_##_ctx.setCycleMode_(Hbs::Context::CycleMode::TRY), taskName_())


#define HBS_SUCCESS(expr_) ((expr_) == Hbs::SUCCESS)
#define HBS_FAILURE(expr_) ((expr_) == Hbs::FAILURE)
#define HBS_FINISHED(expr_) (finished(expr_))
#define HBS_RUNNING(expr_) ((expr_) == Hbs::RUNNING)

// ===========================================================================
// Coro Control Flow Macros
// ===========================================================================

/**
 * @brief Mark the beginning of a coroutine body.
 *
 * Must be matched by a corresponding @c CORO_END, @c CORO_END_SUCCESS,
 * @c CORO_END_FAILURE, or @c CORO_END_WITH at the end of the coroutine.
 */
#define CORO_BEGIN()                                                                                                     \
  switch (ctx.resumePoint_())                                                                                                   \
  {                                                                                                                    \
  case -1:

// internal common yield helper
#define HBS__CORO_YIELD_STATUS(status_, id_) HBS__STATEMENT(ctx.setResumePoint_(id_); return status_; case id_:)

/**
 * Yield with a status passed in as a parameter.
 *
 * This form is very unlikely to be used in normal code and would usually
 * only be used if the status of some called function was being passed through.
 *
 * Caution: yielding SUCCESS or FAILURE implies the coro can cycle so you need
 * to ensure that you can resume immediately after the yield statement in such
 * circumstances.
 */
#define CORO_YIELD_STATUS(status_) HBS__CORO_YIELD_STATUS(status_, __COUNTER__)

/**
 * Yield control back to the caller after performing one tick's worth of work.
 *
 * this is the standard yield and by far the most common form used
 */ 
#define CORO_YIELD() CORO_YIELD_STATUS(Hbs::RUNNING)

/**
 * @brief Yield control back to the caller with a SUCCESS status.
 *
 * This form exists to support the case where a behaviour signals success but
 * must be able to re-execute the terminal success code if ticked again on the
 * next cycle. Typical usage:
 * @code
 * if (failureCondition)
 * {
 *   while (true)
 *   {
 *     failureStateCodeToRepeat;
 *     CORO_YIELD_FAILURE();
 *   }
 * }
 * @endcode
 */
#define CORO_YIELD_SUCCESS() CORO_YIELD_STATUS(Hbs::SUCCESS)

/**
 * @brief Yield control back to the caller with a FAILURE status.
 *
 * This form exists to support the case where a behaviour signals failure but
 * must be able to re-execute the terminal failure code if ticked again on the
 * next cycle. See @c CORO_YIELD_SUCCESS for a usage example.
 */
#define CORO_YIELD_FAILURE() CORO_YIELD_STATUS(Hbs::FAILURE)

// internal helpers
#define HBS__ASSERT_NOT_RUNNING(status_) { auto tmpStatus_ = (status_); HBS_ASSERT(tmpStatus_ != Hbs::RUNNING); }
#define HBS__CORO_RETURN(guard_, status_)                                                                              \
  HBS__STATEMENT(guard_ ctx.setResumePoint_(Hbs::Context::RESUME_RESET); return status_)

/**
 * @brief Return SUCCESS from the coroutine immediately.
 *
 * May be used anywhere in the coroutine body, but not after @c CORO_END.
 */
#define CORO_SUCCESS() HBS__CORO_RETURN( , Hbs::SUCCESS)

/**
 * @brief Return FAILURE from the coroutine immediately.
 *
 * May be used anywhere in the coroutine body, but not after @c CORO_END.
 */
#define CORO_FAILURE() HBS__CORO_RETURN( , Hbs::FAILURE)

/**
 * @brief Return a dynamic status value from the coroutine immediately.
 *
 * Useful for propagating a status received from a child or helper function.
 * Only SUCCESS or FAILURE are valid; passing RUNNING is a runtime assertion
 * failure — use @c CORO_YIELD instead.
 */
#define CORO_RETURN(status_) HBS__CORO_RETURN(HBS__ASSERT_NOT_RUNNING(status_), Hbs::FAILURE)


// internal helper
#define HBS__CORO_END(guardCode_, status_)                                                                             \
  }                                                                                                                    \
  guardCode_ ctx.setResumePoint_(Hbs::Context::RESUME_RESET);                                                          \
  return status_;

/**
 * @brief Close the coroutine body and return SUCCESS.
 *
 * Must appear at the very end of the coroutine body to match @c CORO_BEGIN.
 * @note Must be used as a standalone statement — not inside an @c if or
 *       similar construct.
 */
#define CORO_END_SUCCESS() HBS__CORO_END( , Hbs::SUCCESS)

/**
 * @brief Close the coroutine body and return FAILURE.
 *
 * Must appear at the very end of the coroutine body to match @c CORO_BEGIN.
 * @note Must be used as a standalone statement — not inside an @c if or
 *       similar construct.
 */
#define CORO_END_FAILURE() HBS__CORO_END( , Hbs::FAILURE)

/**
 * @brief Close the coroutine body and return a dynamic status value.
 *
 * Must appear at the very end of the coroutine body to match @c CORO_BEGIN.
 * Only SUCCESS or FAILURE are valid; RUNNING triggers a runtime assertion.
 * @note Must be used as a standalone statement — not inside an @c if or
 *       similar construct.
 */
#define CORO_END_WITH(status_) HBS__CORO_END(HBS__ASSERT_NOT_RUNNING(status_), status_)

/**
 * @brief Close the coroutine body and return SUCCESS.
 *
 * Synonym for @c CORO_END_SUCCESS. Must appear at the very end of the
 * coroutine body to match @c CORO_BEGIN.
 * @note Must be used as a standalone statement — not inside an @c if or
 *       similar construct.
 */
#define CORO_END() CORO_END_SUCCESS()

/**
 * @brief Mark a named stage of progress within a coroutine.
 *
 * Records the current coroutine "state" — a named stage that may last for
 * several ticks and can have an associated duration for diagnostics and
 * tracing purposes.
 */
#define CORO_STATE(stateIndent_) ctx.setCoroState_(#stateIndent_,__LINE__)

// CORO idioms

/**
 * @brief Tick a child task repeatedly, yielding each tick, until it completes.
 *
 * A yield is inserted after every tick of the child, including the final tick
 * that returns SUCCESS or FAILURE (the "tail suspend always" pattern). This
 * prevents accidentally performing two units of work — the awaited task and
 * the statement following @c CORO_AWAIT — within a single behaviour tick.
 *
 * If the extra tail yield is undesirable, use @c CORO_AWAIT_NO_TAIL_YIELD
 * instead.
 */
#define CORO_AWAIT(task_) HBS__STATEMENT(while ((task_) == Hbs::RUNNING) { CORO_YIELD(); } CORO_YIELD();)

/**
 * @brief Tick a child task once for each caller tick, yielding each time, until it completes,
 *        but without yielding after the terminal status.
 *
 * Like @c CORO_AWAIT but without the trailing yield after the child returns
 * SUCCESS or FAILURE. Use this only when you are certain that skipping the
 * tail yield is safe.
 */
#define CORO_AWAIT_NO_TAIL(task_) HBS__STATEMENT(while ((task_) == Hbs::RUNNING) CORO_YIELD();)

/**
 * @brief Tick a statement or block repeatedly until a condition becomes true.
 *
 * Executes @p stmtOrBlock_ and yields on each tick for as long as @p cond_
 * is false. Once @p cond_ is true the body does not execute and control
 * passes immediately to the next statement — there is no tail yield.
 *
 * This is a convenience macro that automatically inserts @c CORO_YIELD. It
 * is suitable for short code blocks; longer blocks may affect line-number
 * attribution in a debugger.
 */
#define CORO_UNTIL(cond_, stmtOrBlock_)                                                                                \
  HBS__STATEMENT(while (!(cond_)) {                                                                                    \
    stmtOrBlock_;                                                                                                      \
    CORO_YIELD();                                                                                                      \
  })

/**
 * @brief Tick a statement or block once for each caller tick while a condition remains true.
 *
 * Executes @p stmtOrBlock_ and yields on each tick for as long as @p cond_
 * is true. Once @p cond_ is false the body does not execute and control
 * passes immediately to the next statement — there is no tail yield.
 *
 * This is a convenience macro that automatically inserts @c CORO_YIELD. It
 * is suitable for short code blocks; longer blocks may affect line-number
 * attribution in a debugger.
 */
#define CORO_WHILE(cond_, stmtOrBlock_)                                                                                \
  HBS__STATEMENT(while (cond_) {                                                                                       \
    stmtOrBlock_;                                                                                                      \
    CORO_YIELD();                                                                                                      \
  })



// ===========================================================================
// Section-tag macros
// ===========================================================================
//
// ARGS, VARS, DEFINES_PARAMS, LOADS_PARAMS are all optional in CORO_DEFINE /
// HSM_DEFINE / CORO_DECLARE / HSM_DECLARE.  Each macro wraps its content in a
// tagged tuple so the outer macro can detect which sections are present.
//
// Elements inside each section are bare tuples:
//   (Type, Name, DefaultVal)   — with default (used in DECLARE and inline DEFINE)
//   (Type, Name)               — without default (used in out-of-class DEFINE)
//
// The HBS__ARG_FIELD / HBS__VAR_STRUCT_FIELD / HBS__PARAM_STRUCT_FIELD helpers
// in HbsMacroUtils.h unpack these tuples and emit the
// initialiser only when a DefaultVal is present.

#define ARGS(...)           (HBS_ARGS_TAG,   __VA_ARGS__)
#define VARS(...)           (HBS_VARS_TAG,   __VA_ARGS__)
#define DEFINES_PARAMS(...) (HBS_PARAMS_TAG, __VA_ARGS__)
#define LOADS_PARAMS(...)   (HBS_PARAMS_TAG, __VA_ARGS__)


// ===========================================================================
// Shared building-block macros (used by both CORO and HSM DECLARE/DEFINE)
// ===========================================================================

// HBS__TASK_MEMBERS_DECLARE — class members for the DECLARE (split) pattern.
// Only ARGS are known here; VARS and PARAMS are defined in the out-of-class DEFINE.
// _Vars and _Params are empty placeholders; _paramsPtr holds the real params lazily.

#define HBS__TASK_MEMBERS_DECLARE(taskName_, ...)                                                                      \
  /* 1. Context Member */                                                                                               \
  Hbs::Context taskName_##_ctx{hbsEnv, #taskName_};                                                                    \
                                                                                                                        \
  /* 2. Args Struct (from ARGS section only) */                                                                         \
  struct taskName_##_Args                                                                                               \
  {                                                                                                                     \
    HBS__MAP_ARGS(HBS__ARG_FIELD, , __VA_ARGS__)                                                                       \
  };                                                                                                                    \
                                                                                                                        \
  /* 3. Vars placeholder — real _Vars defined in hbs_detail namespace by out-of-class DEFINE */ \
  struct taskName_##_Vars : public Hbs::VarStorage {};                                                                  \
                                                                                                                        \
  /* 4. Params placeholder + opaque storage ptr — real _Params in hbs_detail namespace */       \
  struct taskName_##_Params {};                                                                                          \
  std::unique_ptr<Hbs::VarStorage> taskName_##_paramsPtr

// HBS__TASK_MEMBERS — class members for the inline DEFINE pattern.
// Full VARS and PARAMS structs with fields; direct _params member.

#define HBS__TASK_MEMBERS(taskName_, ...)                                                                              \
  /* 1. Context Member */                                                                                               \
  Hbs::Context taskName_##_ctx{hbsEnv, #taskName_};                                                                    \
                                                                                                                        \
  /* 2. Args Struct */                                                                                                  \
  struct taskName_##_Args                                                                                               \
  {                                                                                                                     \
    HBS__MAP_ARGS(HBS__ARG_FIELD, , __VA_ARGS__)                                                                       \
  };                                                                                                                    \
                                                                                                                        \
  /* 3. Vars Struct */                                                                                                  \
  struct taskName_##_Vars : public Hbs::VarStorage                                                                     \
  {                                                                                                                     \
    HBS__MAP_VARS(HBS__VAR_STRUCT_FIELD, , __VA_ARGS__)                                                                \
  };                                                                                                                    \
                                                                                                                        \
  /* 4. Params Struct & Member */                                                                                       \
  struct taskName_##_Params                                                                                             \
  {                                                                                                                     \
    HBS__MAP_PARAMS(HBS__PARAM_STRUCT_FIELD, , __VA_ARGS__)                                                            \
  };                                                                                                                    \
  taskName_##_Params taskName_##_params

// HBS__TASK_ZERO_ARG_WRAPPER — emits the zero-arg template wrapper (item 5).
// Identical for CORO and HSM, and for DECLARE vs inline DEFINE.

#define HBS__TASK_ZERO_ARG_WRAPPER(taskName_)                                                                          \
  template <typename = void>                                                                                            \
  Hbs::Status taskName_()                                                                                               \
    requires std::is_default_constructible_v<taskName_##_Args>                                                         \
  {                                                                                                                     \
    return taskName_(taskName_##_Args{});                                                                               \
  }

// HBS__CORO_WRAPPER — emits the public taskName_(const Args&) wrapper for coroutines.
// Used by CORO_DECLARE (out-of-class split pattern).
// Does NOT call prepareVars_ or pass params — the out-of-class _bodyCode shim does both.
// Calls _bodyCode with the fixed 2-parameter signature (ctx, args) — no VARS, no params.
#define HBS__CORO_WRAPPER(taskName_)                                                                                   \
  Hbs::Status taskName_(const taskName_##_Args &args)                                                                  \
  {                                                                                                                    \
    taskName_##_ctx.beginTick_();                                                                                      \
                                                                                                                       \
    /* Coro Hold-Terminal-Status Semantics: Once terminal, return cached status without re-executing */                \
    if (taskName_##_ctx.finished())                                                                                    \
      return taskName_##_ctx.lastStatus();                                                                             \
                                                                                                                       \
    taskName_##_ctx.beginTrace_();                                                                                     \
                                                                                                                       \
    auto status = taskName_##_bodyCode(taskName_##_ctx, args);                                                         \
                                                                                                                       \
    taskName_##_ctx.setLastStatus_(status);                                                                            \
    taskName_##_ctx.endTrace_();                                                                                       \
    return status;                                                                                                     \
  }

// HBS__CORO_WRAPPER_INLINE — wrapper for inline CORO_DEFINE (no split declaration).
// Calls prepareVars_<taskName_##_Vars>() here because the inline _Vars struct is
// defined with the correct fields by HBS__TASK_MEMBERS in the same expansion.
#define HBS__CORO_WRAPPER_INLINE(taskName_)                                                                            \
  Hbs::Status taskName_(const taskName_##_Args &args)                                                                  \
  {                                                                                                                    \
    taskName_##_ctx.beginTick_();                                                                                      \
                                                                                                                       \
    /* Coro Hold-Terminal-Status Semantics: Once terminal, return cached status without re-executing */                \
    if (taskName_##_ctx.finished())                                                                                    \
      return taskName_##_ctx.lastStatus();                                                                             \
                                                                                                                       \
    taskName_##_ctx.prepareVars_<taskName_##_Vars>();                                                                  \
                                                                                                                       \
    taskName_##_ctx.beginTrace_();                                                                                     \
                                                                                                                       \
    [[maybe_unused]] auto *varsPtr = taskName_##_ctx.getVars_<taskName_##_Vars>();                                     \
    auto status = taskName_##_bodyCode(taskName_##_ctx, args, taskName_##_params);                                     \
                                                                                                                       \
    taskName_##_ctx.setLastStatus_(status);                                                                            \
    taskName_##_ctx.endTrace_();                                                                                       \
    return status;                                                                                                     \
  }

// HBS__HSM_WRAPPER — emits the public taskName_(const Args&) wrapper for HSMs.
// Used by HSM_DECLARE (out-of-class split pattern).
// Does NOT call prepareVars_ or pass params — the out-of-class _bodyCode shim does both.
// Calls _bodyCode with the fixed 2-parameter signature (ctx, args) — no VARS, no params.

#define HBS__HSM_WRAPPER(taskName_)                                                                                    \
  Hbs::Status taskName_(const taskName_##_Args &args)                                                                  \
  {                                                                                                                    \
    taskName_##_ctx.beginTick_();                                                                                      \
                                                                                                                       \
    taskName_##_ctx.beginTrace_();                                                                                     \
                                                                                                                       \
    taskName_##_bodyCode(taskName_##_ctx, args);                                                                       \
    auto status = taskName_##_ctx.lastStatus();                                                                        \
                                                                                                                       \
    taskName_##_ctx.endTrace_();                                                                                       \
    return status;                                                                                                     \
  }

// HBS__HSM_WRAPPER_INLINE — wrapper for inline HSM_DEFINE (no split declaration).
// Calls prepareVars_<taskName_##_Vars>() here because the inline _Vars struct is correct.

#define HBS__HSM_WRAPPER_INLINE(taskName_)                                                                             \
  Hbs::Status taskName_(const taskName_##_Args &args)                                                                  \
  {                                                                                                                    \
    taskName_##_ctx.beginTick_();                                                                                      \
                                                                                                                       \
    taskName_##_ctx.prepareVars_<taskName_##_Vars>();                                                                  \
                                                                                                                       \
    taskName_##_ctx.beginTrace_();                                                                                     \
                                                                                                                       \
    [[maybe_unused]] auto *varsPtr = taskName_##_ctx.getVars_<taskName_##_Vars>();                                     \
    taskName_##_bodyCode(taskName_##_ctx, args, taskName_##_params);                                                   \
    auto status = taskName_##_ctx.lastStatus();                                                                        \
                                                                                                                       \
    taskName_##_ctx.endTrace_();                                                                                       \
    return status;                                                                                                     \
  }

// HBS__CORO_BODYCODE_SIG — fixed 2-parameter _bodyCode signature for coroutines.
// Used by CORO_DECLARE (forward declaration) and by the out-of-class dispatch shim.
// No VARS or params — both are handled inside the shim using hbs_detail namespace types.
// No trailing semicolon — caller appends either ";" (DECLARE) or "{ body }" (shim def).

#define HBS__CORO_BODYCODE_SIG(taskName_)                                                                              \
  Hbs::Status taskName_##_bodyCode(                                                                                    \
      Hbs::Context &ctx, [[maybe_unused]] const taskName_##_Args &args)

// HBS__CORO_BODYCODE_SIG_INLINE — full VARS-expanded _bodyCode signature for inline DEFINE.
// Used only by HBS__CORO_DEFINE_INLINE where declaration and definition are in the same place,
// so no forward-declaration mismatch is possible.

#define HBS__CORO_BODYCODE_SIG_INLINE(taskName_, ...)                                                                  \
  Hbs::Status taskName_##_bodyCode(                                                                                    \
      Hbs::Context &ctx, [[maybe_unused]] const taskName_##_Args &args,                                                \
      [[maybe_unused]] const taskName_##_Params &params                                                                \
      HBS__MAP_VARS(HBS__VAR_ARG, , __VA_ARGS__))

// HBS__HSM_BODYCODE_SIG — fixed 2-parameter _bodyCode signature for HSMs (returns void).
// Used by HSM_DECLARE (forward declaration) and by the out-of-class dispatch shim.

#define HBS__HSM_BODYCODE_SIG(taskName_)                                                                               \
  void taskName_##_bodyCode(                                                                                            \
      Hbs::Context &ctx, [[maybe_unused]] const taskName_##_Args &args)

// HBS__HSM_BODYCODE_SIG_INLINE — full VARS-expanded _bodyCode signature for inline HSM DEFINE.

#define HBS__HSM_BODYCODE_SIG_INLINE(taskName_, ...)                                                                   \
  void taskName_##_bodyCode(                                                                                            \
      Hbs::Context &ctx, [[maybe_unused]] const taskName_##_Args &args,                                               \
      [[maybe_unused]] const taskName_##_Params &params                                                                \
      HBS__MAP_VARS(HBS__VAR_ARG, , __VA_ARGS__))


// ===========================================================================
// CORO_DECLARE and CORO_DEFINE
// ===========================================================================
//

/**
 * Declares a coro inside the executor/container class.
 *
 * Emits everything needed inside the executor class body except
 * the _bodyCode function body opening. Use this when the body will be defined
 * separately out-of-class via CORO_DEFINE((ClassName,taskName),...).
 *
 * @note CORO_DECLARE accepts only ARGS — never VARS or DEFINES_PARAMS. VARS and
 * DEFINES_PARAMS belong exclusively in the out-of-class CORO_DEFINE.
 *
 * Usage (inside class body):
 * @code
 *   CORO_DECLARE( myTask, ARGS( (float, speed, 1.0f) ) )
 * @endcode
 *
 * The ARGS section is optional.
 */

#define CORO_DECLARE(taskName_, ...)                                                                                   \
  HBS__TASK_MEMBERS_DECLARE(taskName_, __VA_ARGS__);                                                                   \
  HBS__TASK_ZERO_ARG_WRAPPER(taskName_)                                                                                \
  HBS__CORO_WRAPPER(taskName_)                                                                                         \
  HBS__CORO_BODYCODE_SIG(taskName_);


/**
 * Defines the variables, configurable parameters, and body of the Coro.
 *
 * Two forms depending on the first (tuple) argument:
 *
 * @code
 *   CORO_DEFINE((taskName), ...)
 * @endcode
 * Inline definition inside the executor class body. Equivalent to CORO_DECLARE
 * followed immediately by the _bodyCode function signature opening. The { body }
 * follows the macro.
 *
 * @code
 *   CORO_DEFINE((ClassName, taskName), ...)
 * @endcode
 * Out-of-class definition (e.g. in a .cpp file). Emits only the
 * ClassName::taskName_bodyCode(...) signature opening. The { body } follows the
 * macro. Defaults should be omitted from element tuples here.
 *
 * All section arguments (ARGS, VARS, DEFINES_PARAMS) are optional.
 */

#define CORO_DEFINE(nameTuple_, ...)  HBS__CORO_DEFINE_DISPATCH(nameTuple_, __VA_ARGS__)

// --- Inline CORO_DEFINE implementation ---
// Emits all members + wrappers + _bodyCode signature opening (no semicolon).
// Uses the full VARS-expanded inline signature — no forward-declaration mismatch possible.

#define HBS__CORO_DEFINE_INLINE(taskName_, ...)                                                                        \
  HBS__TASK_MEMBERS(taskName_, __VA_ARGS__);                                                                           \
  HBS__TASK_ZERO_ARG_WRAPPER(taskName_)                                                                                \
  HBS__CORO_WRAPPER_INLINE(taskName_)                                                                                  \
  HBS__CORO_BODYCODE_SIG_INLINE(taskName_, __VA_ARGS__)

// --- Out-of-class CORO_DEFINE implementation ---
//
// Wrapper dispatch technique inspired by CABSL (https://github.com/bhuman/CABSL).
//
// Problem: XXX_DECLARE (in the .h) only knows ARGS, so it forward-declares
// _bodyCode with a fixed 2-parameter signature (ctx, args).  VARS and PARAMS
// are unknown at DECLARE time and cannot appear in the forward declaration.
// But the user's body needs VARS as individual typed references and PARAMS as
// a typed struct.
//
// Solution: emit three things in the out-of-class DEFINE —
//
//   1. A private namespace hbs_detail_##className_ containing:
//      - taskName_##_Vars  (inherits VarStorage, has the real VARS fields)
//      - taskName_##_Params (inherits VarStorage, has the real PARAMS fields)
//      - taskName_##_Wrapper (inherits className_, declares _impl with full sig)
//
//   2. The definition of className_::taskName_##_bodyCode (the fixed 2-param
//      method forward-declared by CORO_DECLARE).  It lazily initialises
//      _paramsPtr with the namespace _Params type, calls prepareVars_ with the
//      namespace _Vars type, then reinterpret_casts `this` to the wrapper struct
//      and calls _impl, forwarding params and all VARS by reference.
//
//   3. Opens the wrapper struct's _impl signature for the user's { body }.
//      Uses unqualified taskName_##_Args (resolved via _Wrapper's base class)
//      to avoid protected-access errors when DECLARE is in a protected section.
//
// The reinterpret_cast is safe because the wrapper struct adds no data members;
// it only adds methods, so its layout is identical to className_.

#define HBS__CORO_DEFINE_OUTOFCLASS(className_, taskName_, ...)                                                        \
  /* 1. Private namespace: _Vars, _Params with real fields, plus wrapper struct */          \
  namespace hbs_detail_##className_                                                          \
  {                                                                                          \
    struct taskName_##_Vars : public Hbs::VarStorage                                         \
    {                                                                                        \
      HBS__MAP_VARS(HBS__VAR_STRUCT_FIELD, , __VA_ARGS__)                                    \
    };                                                                                       \
    struct taskName_##_Params : public Hbs::VarStorage                                       \
    {                                                                                        \
      HBS__MAP_PARAMS(HBS__PARAM_STRUCT_FIELD, , __VA_ARGS__)                                \
    };                                                                                       \
    struct taskName_##_Wrapper : public className_                                           \
    {                                                                                        \
      Hbs::Status taskName_##_impl(                                                          \
          Hbs::Context &ctx,                                                                 \
          [[maybe_unused]] const className_::taskName_##_Args &args,                         \
          [[maybe_unused]] hbs_detail_##className_::taskName_##_Params &params               \
          HBS__MAP_VARS(HBS__VAR_ARG, , __VA_ARGS__));                                       \
    };                                                                                       \
  }                                                                                          \
  /* 2. Definition of the fixed-signature _bodyCode declared by CORO_DECLARE */             \
  /* Lazily initialises _paramsPtr and prepareVars_ with the correct namespace types */     \
  Hbs::Status className_::taskName_##_bodyCode(                                              \
      Hbs::Context &ctx,                                                                     \
      [[maybe_unused]] const className_::taskName_##_Args &args)                             \
  {                                                                                          \
    if (!taskName_##_paramsPtr)                                                              \
      taskName_##_paramsPtr.reset(                                                           \
          new hbs_detail_##className_::taskName_##_Params());                                \
    auto &params = *static_cast<hbs_detail_##className_::taskName_##_Params *>(             \
        taskName_##_paramsPtr.get());                                                        \
    ctx.prepareVars_<hbs_detail_##className_::taskName_##_Vars>();                           \
    [[maybe_unused]] auto *varsPtr =                                                         \
        ctx.getVars_<hbs_detail_##className_::taskName_##_Vars>();                           \
    return reinterpret_cast<hbs_detail_##className_::taskName_##_Wrapper *>(this)            \
        ->taskName_##_impl(ctx, args, params                                                 \
            HBS__MAP_VARS(HBS__VAR_REF, , __VA_ARGS__));                                     \
  }                                                                                          \
  /* 3. Open _impl signature — user's { body } attaches here */                             \
  /* Use unqualified taskName_##_Args/_Params: resolved via _Wrapper's base class,         \
     avoiding protected-access errors from explicit className_:: qualification */            \
  Hbs::Status hbs_detail_##className_::taskName_##_Wrapper::taskName_##_impl(               \
      Hbs::Context &ctx,                                                                     \
      [[maybe_unused]] const taskName_##_Args &args,                                         \
      [[maybe_unused]] hbs_detail_##className_::taskName_##_Params &params                   \
      HBS__MAP_VARS(HBS__VAR_ARG, , __VA_ARGS__))


// ===========================================================================
// HSM Control Flow Macros
// ===========================================================================

/**
 * @brief Marker for the global transitions block.
 *
 * Place this block before any state declarations if used. Transitions
 * written here are evaluated on every tick regardless of the current state.
 *
 * @code
 * HSM_GLOBAL_TRANSITIONS { if (cond1) HSM_GOTO(someState); else ... }
 * @endcode
 */
#define HSM_GLOBAL_TRANSITIONS

/**
 * @brief Transition checks within a state body, skipped on the first entry
 *        tick.
 *
 * Prevents chains of transitions by not evaluating the block on the tick
 * that enters the state (except for the initial state). Write the transitions 
 * as the first block in any state.
 *
 * @code
 * HSM_TRANSITIONS { if (cond1) HSM_GOTO(someState); else ... }
 * @endcode
 */
#define HSM_TRANSITIONS if (ctx.transitionsEnabled_())

/**
 * @brief Entry code block — only executes on the first tick in a state.
 *
 * Write this as the second block in any state where one-time initialisation
 * is needed.
 *
 * @code
 * HSM_ON_ENTRY { oneTimeStateEntryCode... }
 * @endcode
 */
#define HSM_ON_ENTRY  if (ctx.isStateEntry_())

/**
 * @brief Transition to another state.
 *
 * Use within @ref HSM_TRANSITIONS or @ref HSM_GLOBAL_TRANSITIONS only —
 * nowhere else. Usually written as:
 * @code
 * if (someCond) HSM_GOTO(someState);
 * @endcode
 * To obtain UML2 transition-action semantics, place action code before the
 * macro:
 * @code
 * if (someCond) { someActionCode...; HSM_GOTO(someState); }
 * @endcode
 * Action code must be run-to-completion and must not involve anything
 * time-consuming.
 */
#define HSM_GOTO(_target) goto _target

// Internal implementation
#define HBS__HSM_STATE_I(stateIdent_, stateAlias_, stateLine_, status_, transitionsOnEntry_)                           \
  if (ctx.isAtStart_()) goto initial_state;                                                                            \
  if ((ctx.isAtState_(stateLine_)) && (ctx.isStateEntry_()))                                                           \
  {                                                                                                                    \
    stateAlias_                                                                                                        \
    stateIdent_ : ctx.enterHsmState_(stateLine_, #stateIdent_, status_, transitionsOnEntry_);                          \
  }                                                                                                                    \
  if (ctx.isAtState_(stateLine_))

/**
 * @brief Define a normal (running) HSM state.
 *
 * @code
 * HSM_STATE(someIdentifier) { stateCode... }
 * @endcode
 */
#define HSM_STATE(stateIdent_) \
    HBS__HSM_STATE_I(stateIdent_, , __LINE__, Hbs::RUNNING, false)

/**
 * @brief Define the initial state of the HSM — there can only be one.
 *
 * The HSM enters this state on the very first tick.
 */
#define HSM_INITIAL_STATE(stateIdent_) \
    if (ctx.isAtState_(-1)) goto stateIdent_; \
    HBS__HSM_STATE_I(stateIdent_, initial_state:, __LINE__, Hbs::RUNNING, true)

/**
 * @brief Define a success terminal state.
 *
 * When the HSM reaches this state it returns @c Hbs::SUCCESS status to
 * its callers.
 */
#define HSM_SUCCESS_STATE(stateIdent_) \
    HBS__HSM_STATE_I(stateIdent_, , __LINE__, Hbs::SUCCESS, false)

/**
 * @brief Define a failure terminal state.
 *
 * When the HSM reaches this state it returns @c Hbs::FAILURE status to
 * its callers.
 */
#define HSM_FAILURE_STATE(stateIdent_) \
    HBS__HSM_STATE_I(stateIdent_, , __LINE__, Hbs::FAILURE, false)


// ===========================================================================
// HSM_DECLARE and HSM_DEFINE
// ===========================================================================

/**
 * Declares the HSM inside the executor/container class.
 *
 * Emits everything needed inside the executor class body except
 * the _bodyCode function body opening. Use this when the HSM body will be
 * defined separately out-of-class via HSM_DEFINE((ClassName,taskName),...).
 *
 * @note HSM_DECLARE accepts only ARGS — never VARS or DEFINES_PARAMS. VARS and
 * DEFINES_PARAMS belong exclusively in the out-of-class HSM_DEFINE.
 *
 * Usage (inside class body):
 * @code
 *   HSM_DECLARE( myHsm, ARGS( (float, speed, 1.0f) ) )
 * @endcode
 *
 * The ARGS section is optional.
 */

#define HSM_DECLARE(taskName_, ...)                  \
  HBS__TASK_MEMBERS_DECLARE(taskName_, __VA_ARGS__); \
  HBS__TASK_ZERO_ARG_WRAPPER(taskName_)              \
  HBS__HSM_WRAPPER(taskName_)                        \
  HBS__HSM_BODYCODE_SIG(taskName_);


/**
 * Defines the variables, configurabla parameters and body of a HSM.
 *
 * Two forms depending on the first (tuple) argument:
 *
 * @code
 *   HSM_DEFINE((taskName), ...)
 * @endcode
 * Inline definition inside the executor class body.
 *
 * @code
 *   HSM_DEFINE((ClassName, taskName), ...)
 * @endcode
 * Out-of-class definition (e.g. in a .cpp file).
 *
 * All section arguments are optional.
 */

#define HSM_DEFINE(nameTuple_, ...)  HBS__HSM_DEFINE_DISPATCH(nameTuple_, __VA_ARGS__)

// --- Inline HSM_DEFINE implementation ---
// Emits all members + wrappers + _bodyCode signature opening (no semicolon).
// Uses the full VARS-expanded inline signature — no forward-declaration mismatch possible.

#define HBS__HSM_DEFINE_INLINE(taskName_, ...)  \
  HBS__TASK_MEMBERS(taskName_, __VA_ARGS__);    \
  HBS__TASK_ZERO_ARG_WRAPPER(taskName_)         \
  HBS__HSM_WRAPPER_INLINE(taskName_)            \
  HBS__HSM_BODYCODE_SIG_INLINE(taskName_, __VA_ARGS__)

// --- Out-of-class HSM_DEFINE implementation ---
//
// Wrapper dispatch technique inspired by CABSL (https://github.com/bhuman/CABSL).
//
// Same pattern as HBS__CORO_DEFINE_OUTOFCLASS but for HSMs (void return type).
// See that macro for a full explanation of the technique.

#define HBS__HSM_DEFINE_OUTOFCLASS(className_, taskName_, ...)                                                         \
  /* 1. Private namespace: _Vars, _Params with real fields, plus wrapper struct */          \
  namespace hbs_detail_##className_                                                          \
  {                                                                                          \
    struct taskName_##_Vars : public Hbs::VarStorage                                         \
    {                                                                                        \
      HBS__MAP_VARS(HBS__VAR_STRUCT_FIELD, , __VA_ARGS__)                                    \
    };                                                                                       \
    struct taskName_##_Params : public Hbs::VarStorage                                       \
    {                                                                                        \
      HBS__MAP_PARAMS(HBS__PARAM_STRUCT_FIELD, , __VA_ARGS__)                                \
    };                                                                                       \
    struct taskName_##_Wrapper : public className_                                           \
    {                                                                                        \
      void taskName_##_impl(                                                                 \
          Hbs::Context &ctx,                                                                 \
          [[maybe_unused]] const className_::taskName_##_Args &args,                         \
          [[maybe_unused]] hbs_detail_##className_::taskName_##_Params &params               \
          HBS__MAP_VARS(HBS__VAR_ARG, , __VA_ARGS__));                                       \
    };                                                                                       \
  }                                                                                          \
  /* 2. Definition of the fixed-signature _bodyCode declared by HSM_DECLARE */              \
  /* Lazily initialises _paramsPtr and prepareVars_ with the correct namespace types */     \
  void className_::taskName_##_bodyCode(                                                     \
      Hbs::Context &ctx,                                                                     \
      [[maybe_unused]] const className_::taskName_##_Args &args)                             \
  {                                                                                          \
    if (!taskName_##_paramsPtr)                                                              \
      taskName_##_paramsPtr.reset(                                                           \
          new hbs_detail_##className_::taskName_##_Params());                                \
    auto &params = *static_cast<hbs_detail_##className_::taskName_##_Params *>(             \
        taskName_##_paramsPtr.get());                                                        \
    ctx.prepareVars_<hbs_detail_##className_::taskName_##_Vars>();                           \
    [[maybe_unused]] auto *varsPtr =                                                         \
        ctx.getVars_<hbs_detail_##className_::taskName_##_Vars>();                           \
    reinterpret_cast<hbs_detail_##className_::taskName_##_Wrapper *>(this)                   \
        ->taskName_##_impl(ctx, args, params                                                 \
            HBS__MAP_VARS(HBS__VAR_REF, , __VA_ARGS__));                                     \
  }                                                                                          \
  /* 3. Open _impl signature — user's { body } attaches here */                             \
  /* Use unqualified taskName_##_Args/_Params: resolved via _Wrapper's base class,         \
     avoiding protected-access errors from explicit className_:: qualification */            \
  void hbs_detail_##className_::taskName_##_Wrapper::taskName_##_impl(                      \
      Hbs::Context &ctx,                                                                     \
      [[maybe_unused]] const taskName_##_Args &args,                                         \
      [[maybe_unused]] hbs_detail_##className_::taskName_##_Params &params                   \
      HBS__MAP_VARS(HBS__VAR_ARG, , __VA_ARGS__))


// ===========================================================================
// BT Sequence / Fallback Combinators and support macros
// ===========================================================================

/// cache the terminal status to prevent BT child rerunning - acts like BT memory semantics on a per child basis
#define HBS_HOLD(expr_)   ctx.applyBtSlot_([&]() -> Hbs::Status { return (expr_); }, /* defer: */ false)

/// defer the terminal status by one tick (useful to prevent double actuations in a sequence of actuations).
/// HBS_DEFER implies HBS_HOLD semantics also, i.e. it does not rerun until the entire BT combinator terminates
#define HBS_DEFER(expr_)  ctx.applyBtSlot_([&]() -> Hbs::Status { return (expr_); }, /* defer: */ true)


// BT_SEQUENCE support
// -------------------

// Applied to each step expression.
// SUCCESS  : fall through to next step (REACTIVE default — same tick advancement).
// FAILURE  : sequence fails immediately.
// RUNNING  : sequence running.
// INITIAL  : mapped to FAILURE (child opted out / not applicable).
#define HBS__BT_SEQ_STEP(expr_)                                                                                        \
  {                                                                                                                    \
    Hbs::Status s = (expr_);                                                                                           \
    if (s == Hbs::INITIAL)                                                                                             \
      s = Hbs::FAILURE;                                                                                                \
    if (s != Hbs::SUCCESS)                                                                                             \
      return s;                                                                                                        \
  }

/**
 * Reactive behaviour tree sequence combinator.
 *
 * Evaluates children left-to-right every tick with reactive semantics: a
 * succeeding child immediately passes control to the next child within the same
 * tick. Returns SUCCESS when all children succeed, FAILURE as soon as any child
 * fails, and RUNNING while a child is still running. A child returning INITIAL
 * is treated as FAILURE.
 *
 * Children are tasks or ordinary functions that return a Hbs::Status. Wrap with
 * HBS_HOLD to cache a terminal result across ticks (avoiding re-evaluation), or
 * HBS_DEFER to insert a one-tick gap before advancing — useful for children that
 * set actuation on their terminal tick.
 *
 * @warning At most one BT combinator may be evaluated per tick within a given
 * task context. Evaluating two combinators in the same tick is not supported
 * and will corrupt the internal state.
 *
 * @code
 * // condition re-evaluated reactively; action held once complete; actuating
 * // step deferred one tick to separate its actuation from the next child
 * auto result = BT_SEQUENCE(ballVisible(), 
 *                           HBS_HOLD(approach()),
 *                           HBS_DEFER(kick()));
 * @endcode
 */
#define BT_SEQUENCE(...)                                                                                               \
  (                                                                                                                    \
      [&]() -> Hbs::Status                                                                                             \
      {                                                                                                                \
        constexpr uint16_t combId = static_cast<uint16_t>(__COUNTER__);                                                \
        ctx.beginBtCombinator_(combId);                                                                                \
        HBS__MAPC(HBS__BT_SEQ_STEP, , __VA_ARGS__)                                                                     \
        return Hbs::SUCCESS;                                                                                           \
      }())


// ===========================================================================
// BT_FALLBACK helpers
// ===========================================================================

// Mirror of BT__SEQ_STEP: advances on FAILURE, stops on SUCCESS or RUNNING.
// INITIAL  : mapped to FAILURE (child opted out, fallback tries next).
#define HBS__BT_FALLBACK_STEP(expr_)  \
  {                               \
    Hbs::Status s = (expr_);      \
    if (s == Hbs::INITIAL)        \
      s = Hbs::FAILURE;           \
    if (s != Hbs::FAILURE)        \
      return s;                   \
  }

// ===========================================================================
// BT_FALLBACK
// ===========================================================================
/**
 * Reactive behaviour tree fallback combinator.
 *
 * Evaluates children left-to-right every tick with reactive semantics: a failing
 * child immediately passes control to the next child within the same tick.
 * Returns FAILURE when all children fail, SUCCESS as soon as any child succeeds,
 * and RUNNING while a child is still running. A child returning INITIAL is
 * treated as FAILURE (child not applicable; try the next).
 *
 * Children are tasks or ordinary functions that return a Hbs::Status. Wrap with
 * HBS_HOLD to cache a terminal result across ticks, or HBS_DEFER to insert a
 * one-tick gap before advancing.
 *
 * @warning At most one BT combinator may be evaluated per tick within a given
 * task context. Evaluating two combinators in the same tick is not supported
 * and will corrupt the internal state.
 *
 * @code
 * // try primary behaviour; if it fails, hold the fallback result once reached
 * auto result = BT_FALLBACK(HBS_HOLD(primaryAction()),
 *                           HBS_HOLD(fallbackAction()));
 * @endcode
 */
#define BT_FALLBACK(...)                                                                                               \
  (                                                                                                                    \
      [&]() -> Hbs::Status                                                                                             \
      {                                                                                                                \
        constexpr uint16_t combId = static_cast<uint16_t>(__COUNTER__);                                                \
        ctx.beginBtCombinator_(combId);                                                                                \
        HBS__MAPC(HBS__BT_FALLBACK_STEP, , __VA_ARGS__)                                                                \
        return Hbs::FAILURE;                                                                                           \
      }())


/*
// ===========================================================================
// Example Expansion
// ===========================================================================

// ---------------------------------------------------------------------------
// Pattern A — fully inline (no separate declaration needed):
//
// CORO_DEFINE( (myTask),
//   ARGS( (float, speed, 1.0f) ),
//   VARS( (int, step, 0) ),
//   DEFINES_PARAMS( (float, threshold, 0.5f) )
// )
// { CORO_BEGIN() ... CORO_END() }
//
// Generated Code (simplified):
//
// // 1. Context Member
// Hbs::Context myTask_ctx{hbsEnv, "myTask"};
//
// // 2. Args Struct
// struct myTask_Args { float speed {1.0f}; };
//
// // 3. Vars Struct
// struct myTask_Vars : public Hbs::VarStorage { int step {0}; };
//
// // 4. Params Struct & Member
// struct myTask_Params { float threshold {0.5f}; };
// myTask_Params myTask_params;
//
// // 5. Zero-arg wrapper
// template <typename = void>
// Hbs::Status myTask() requires std::is_default_constructible_v<myTask_Args>
// { return myTask(myTask_Args{}); }
//
// // 6. Public wrapper (inline — calls prepareVars_ and passes params directly)
// Hbs::Status myTask(const myTask_Args& args) {
//   myTask_ctx.beginTick_();
//   if (myTask_ctx.finished()) return myTask_ctx.lastStatus();
//   myTask_ctx.prepareVars_<myTask_Vars>();
//   auto* varsPtr = myTask_ctx.getVars_<myTask_Vars>();
//   auto status = myTask_bodyCode(myTask_ctx, args, myTask_params, varsPtr->step);
//   myTask_ctx.setLastStatus_(status);
//   return status;
// }
//
// // 7. _bodyCode signature opening — { body } follows
// Hbs::Status myTask_bodyCode(Hbs::Context& ctx,
//     [[maybe_unused]] const myTask_Args& args,
//     [[maybe_unused]] const myTask_Params& params,
//     [[maybe_unused]] int& step)
// { CORO_BEGIN() ... CORO_END() }

// ---------------------------------------------------------------------------
// Pattern B — split declare/define, definition in .cpp:
//
// IMPORTANT: CORO_DECLARE accepts only ARGS — never VARS or DEFINES_PARAMS.
// VARS and DEFINES_PARAMS belong exclusively in the out-of-class CORO_DEFINE.
//
// // In executor .h (inside class body):
// CORO_DECLARE( myTask, ARGS( (float, speed, 1.0f) ) )
//
// // In myTask.cpp (VARS/PARAMS here; defaults omitted from ARGS tuples):
// CORO_DEFINE( (MyExecutor, myTask),
//   ARGS( (float, speed) ),
//   VARS( (int, step) ),
//   DEFINES_PARAMS( (float, threshold) )
// )
// { CORO_BEGIN() ... CORO_END() }
//
// DECLARE emits (inside the class):
//   - myTask_ctx, myTask_Args (from ARGS), empty myTask_Vars placeholder,
//     empty myTask_Params placeholder, myTask_paramsPtr (unique_ptr<VarStorage>)
//   - zero-arg wrapper
//   - public wrapper: calls myTask_bodyCode(ctx, args)  [2-param, no VARS/params]
//   - forward declaration: Hbs::Status myTask_bodyCode(ctx, args);
//
// Out-of-class DEFINE emits (wrapper dispatch, inspired by CABSL):
//   namespace hbs_detail_MyExecutor {
//     struct myTask_Vars : VarStorage { int step; };
//     struct myTask_Params : VarStorage { float threshold; };
//     struct myTask_Wrapper : public MyExecutor {
//       Hbs::Status myTask_impl(ctx, args, myTask_Params& params, int& step);
//     };
//   }
//   // Shim: defines the 2-param _bodyCode, lazily inits params, calls _impl
//   Hbs::Status MyExecutor::myTask_bodyCode(ctx, args) {
//     if (!myTask_paramsPtr) myTask_paramsPtr.reset(new hbs_detail_MyExecutor::myTask_Params());
//     auto& params = *static_cast<hbs_detail_MyExecutor::myTask_Params*>(myTask_paramsPtr.get());
//     ctx.prepareVars_<hbs_detail_MyExecutor::myTask_Vars>();
//     auto* varsPtr = ctx.getVars_<hbs_detail_MyExecutor::myTask_Vars>();
//     return reinterpret_cast<hbs_detail_MyExecutor::myTask_Wrapper*>(this)
//         ->myTask_impl(ctx, args, params, varsPtr->step);
//   }
//   // User body attaches here:
//   Hbs::Status hbs_detail_MyExecutor::myTask_Wrapper::myTask_impl(
//       ctx, args, params, int& step)
//   { CORO_BEGIN() ... CORO_END() }
*/

