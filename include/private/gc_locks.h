/* 
 * Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 * Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 * Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 * Copyright (c) 1999 by Hewlett-Packard Company. All rights reserved.
 *
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 */

#ifndef GC_LOCKS_H
#define GC_LOCKS_H

#include <atomic_ops.h>

/*
 * Mutual exclusion between allocator/collector routines.
 * Needed if there is more than one allocator thread.
 * FASTLOCK() is assumed to try to acquire the lock in a cheap and
 * dirty way that is acceptable for a few instructions, e.g. by
 * inhibiting preemption.  This is assumed to have succeeded only
 * if a subsequent call to FASTLOCK_SUCCEEDED() returns TRUE.
 * FASTUNLOCK() is called whether or not FASTLOCK_SUCCEEDED().
 * If signals cannot be tolerated with the FASTLOCK held, then
 * FASTLOCK should disable signals.  The code executed under
 * FASTLOCK is otherwise immune to interruption, provided it is
 * not restarted.
 * DCL_LOCK_STATE declares any local variables needed by LOCK and UNLOCK
 * and/or FASTLOCK.
 *
 * In the PARALLEL_MARK case, we also need to define a number of
 * other inline finctions here:
 *   GC_bool GC_compare_and_exchange( volatile GC_word *addr,
 *   				      GC_word old, GC_word new )
 *   GC_word GC_atomic_add( volatile GC_word *addr, GC_word how_much )
 *   void GC_memory_barrier( )
 *   
 */  
# ifdef THREADS
   void GC_noop1(word);
#  ifdef PCR_OBSOLETE	/* Faster, but broken with multiple lwp's	*/
#    include  "th/PCR_Th.h"
#    include  "th/PCR_ThCrSec.h"
     extern struct PCR_Th_MLRep GC_allocate_ml;
#    define DCL_LOCK_STATE  PCR_sigset_t GC_old_sig_mask
#    define LOCK() PCR_Th_ML_Acquire(&GC_allocate_ml) 
#    define UNLOCK() PCR_Th_ML_Release(&GC_allocate_ml)
#    define UNLOCK() PCR_Th_ML_Release(&GC_allocate_ml)
#    define FASTLOCK() PCR_ThCrSec_EnterSys()
     /* Here we cheat (a lot): */
#        define FASTLOCK_SUCCEEDED() (*(int *)(&GC_allocate_ml) == 0)
		/* TRUE if nobody currently holds the lock */
#    define FASTUNLOCK() PCR_ThCrSec_ExitSys()
#  endif
#  ifdef PCR
#    include <base/PCR_Base.h>
#    include <th/PCR_Th.h>
     extern PCR_Th_ML GC_allocate_ml;
#    define DCL_LOCK_STATE \
	 PCR_ERes GC_fastLockRes; PCR_sigset_t GC_old_sig_mask
#    define LOCK() PCR_Th_ML_Acquire(&GC_allocate_ml)
#    define UNLOCK() PCR_Th_ML_Release(&GC_allocate_ml)
#    define FASTLOCK() (GC_fastLockRes = PCR_Th_ML_Try(&GC_allocate_ml))
#    define FASTLOCK_SUCCEEDED() (GC_fastLockRes == PCR_ERes_okay)
#    define FASTUNLOCK()  {\
        if( FASTLOCK_SUCCEEDED() ) PCR_Th_ML_Release(&GC_allocate_ml); }
#  endif

#  if !defined(AO_have_test_and_set_acquire)
#    define USE_PTHREAD_LOCKS
#  endif


#  if defined(GC_PTHREADS) && !defined(GC_WIN32_THREADS)
#    define NO_THREAD (pthread_t)(-1)
#    include <pthread.h>

#    if !defined(THREAD_LOCAL_ALLOC) && !defined(USE_PTHREAD_LOCKS)
      /* In the THREAD_LOCAL_ALLOC case, the allocation lock tends to	*/
      /* be held for long periods, if it is held at all.  Thus spinning	*/
      /* and sleeping for fixed periods are likely to result in 	*/
      /* significant wasted time.  We thus rely mostly on queued locks. */
#     define USE_SPIN_LOCK
      extern volatile unsigned int GC_allocate_lock;
      extern void GC_lock(void);
	/* Allocation lock holder.  Only set if acquired by client through */
	/* GC_call_with_alloc_lock.					   */
#     ifdef GC_ASSERTIONS
#        define UNCOND_LOCK() \
		{ if (AO_test_and_set_acquire(&GC_allocate_lock)) GC_lock(); \
		  SET_LOCK_HOLDER(); }
#        define UNCOND_UNLOCK() \
		{ GC_ASSERT(I_HOLD_LOCK()); UNSET_LOCK_HOLDER(); \
	          AO_CLEAR(&GC_allocate_lock); }
#     else
#        define UNCOND_LOCK() \
		{ if (AO_test_and_set_acquire(&GC_allocate_lock)) GC_lock(); }
#        define UNCOND_UNLOCK() \
		AO_CLEAR(&GC_allocate_lock)
#     endif /* !GC_ASSERTIONS */
#    else /* THREAD_LOCAL_ALLOC  || USE_PTHREAD_LOCKS */
#      ifndef USE_PTHREAD_LOCKS
#        define USE_PTHREAD_LOCKS
#      endif
#    endif /* THREAD_LOCAL_ALLOC || USE_PTHREAD_LOCK */
#    ifdef USE_PTHREAD_LOCKS
#      include <pthread.h>
       extern pthread_mutex_t GC_allocate_ml;
#      ifdef GC_ASSERTIONS
#        define UNCOND_LOCK() \
		{ GC_lock(); \
		  SET_LOCK_HOLDER(); }
#        define UNCOND_UNLOCK() \
		{ GC_ASSERT(I_HOLD_LOCK()); UNSET_LOCK_HOLDER(); \
	          pthread_mutex_unlock(&GC_allocate_ml); }
#      else /* !GC_ASSERTIONS */
#        if defined(NO_PTHREAD_TRYLOCK)
#          define UNCOND_LOCK() GC_lock();
#        else /* !defined(NO_PTHREAD_TRYLOCK) */
#        define UNCOND_LOCK() \
	   { if (0 != pthread_mutex_trylock(&GC_allocate_ml)) GC_lock(); }
#        endif
#        define UNCOND_UNLOCK() pthread_mutex_unlock(&GC_allocate_ml)
#      endif /* !GC_ASSERTIONS */
#    endif /* USE_PTHREAD_LOCKS */
#    define SET_LOCK_HOLDER() GC_lock_holder = pthread_self()
#    define UNSET_LOCK_HOLDER() GC_lock_holder = NO_THREAD
#    define I_HOLD_LOCK() (!GC_need_to_lock \
			   || pthread_equal(GC_lock_holder, pthread_self()))
     extern volatile GC_bool GC_collecting;
#    define ENTER_GC() GC_collecting = 1;
#    define EXIT_GC() GC_collecting = 0;
     extern void GC_lock(void);
     extern pthread_t GC_lock_holder;
#    ifdef GC_ASSERTIONS
      extern pthread_t GC_mark_lock_holder;
#    endif
#  endif /* GC_PTHREADS with linux_threads.c implementation */

#  if defined(GC_WIN32_THREADS)
#    if defined(GC_PTHREADS)
#      include <pthread.h>
       extern pthread_mutex_t GC_allocate_ml;
#      define UNCOND_LOCK()   pthread_mutex_lock(&GC_allocate_ml)
#      define UNCOND_UNLOCK() pthread_mutex_unlock(&GC_allocate_ml)
#    else
#      include <windows.h>
       GC_API CRITICAL_SECTION GC_allocate_ml;
#      define UNCOND_LOCK() EnterCriticalSection(&GC_allocate_ml);
#      define UNCOND_UNLOCK() LeaveCriticalSection(&GC_allocate_ml);
#    endif
#  endif
#  ifndef SET_LOCK_HOLDER
#      define SET_LOCK_HOLDER()
#      define UNSET_LOCK_HOLDER()
#      define I_HOLD_LOCK() FALSE
		/* Used on platforms were locks can be reacquired,	*/
		/* so it doesn't matter if we lie.			*/
#  endif
# else /* !THREADS */
#    define LOCK()
#    define UNLOCK()
# endif /* !THREADS */

#if defined(UNCOND_LOCK) && !defined(LOCK) 
     GC_API GC_bool GC_need_to_lock;
     		/* At least two thread running; need to lock.	*/
#    define LOCK() if (GC_need_to_lock) { UNCOND_LOCK(); }
#    define UNLOCK() if (GC_need_to_lock) { UNCOND_UNLOCK(); }
#endif

# ifndef SET_LOCK_HOLDER
#   define SET_LOCK_HOLDER()
#   define UNSET_LOCK_HOLDER()
#   define I_HOLD_LOCK() FALSE
		/* Used on platforms were locks can be reacquired,	*/
		/* so it doesn't matter if we lie.			*/
# endif

# ifndef ENTER_GC
#   define ENTER_GC()
#   define EXIT_GC()
# endif

# ifndef DCL_LOCK_STATE
#   define DCL_LOCK_STATE
# endif

# ifndef FASTLOCK
#   define FASTLOCK() LOCK()
#   define FASTLOCK_SUCCEEDED() TRUE
#   define FASTUNLOCK() UNLOCK()
# endif

#endif /* GC_LOCKS_H */
