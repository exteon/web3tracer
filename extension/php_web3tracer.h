/*
 *	Copyright (c) 2012 EXTEON
 * 
 *  Portions Copyright (c) 2009 Facebook, from project XHPROF, covered under the Apache License v 2.0
 *
 *  Licensed under the Apache License, Version 2.0
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#ifndef PHP_WEB3TRACER_H
#define PHP_WEB3TRACER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef linux
/* To enable CPU_ZERO and CPU_SET, etc.     */
# define _GNU_SOURCE
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "Zend/zend_extensions.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <unistd.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>


#ifdef __FreeBSD__
# if __FreeBSD_version >= 700110
#   include <sys/resource.h>
#   include <sys/cpuset.h>
#   define cpu_set_t cpuset_t
#   define SET_AFFINITY(pid, size, mask) \
           cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, size, mask)
#   define GET_AFFINITY(pid, size, mask) \
           cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, size, mask)
# else
#   error "This version of FreeBSD does not support cpusets"
# endif /* __FreeBSD_version */
#elif __APPLE__
/*
 * Patch for compiling in Mac OS X Leopard
 * @author Svilen Spasov <s.spasov@gmail.com> 
 */
#    include <mach/mach_init.h>
#    include <mach/thread_policy.h>
#    define cpu_set_t thread_affinity_policy_data_t
#    define CPU_SET(cpu_id, new_mask) \
        (*(new_mask)).affinity_tag = (cpu_id + 1)
#    define CPU_ZERO(new_mask)                 \
        (*(new_mask)).affinity_tag = THREAD_AFFINITY_TAG_NULL
#   define SET_AFFINITY(pid, size, mask)       \
        thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY, mask, \
                          THREAD_AFFINITY_POLICY_COUNT)
#else
/* For sched_getaffinity, sched_setaffinity */
# include <sched.h>
# define SET_AFFINITY(pid, size, mask) sched_setaffinity(0, size, mask)
# define GET_AFFINITY(pid, size, mask) sched_getaffinity(0, size, mask)
#endif /* __FreeBSD__ */

extern zend_module_entry web3tracer_module_entry;
#define phpext_web3tracer_ptr &web3tracer_module_entry

#ifdef PHP_WIN32
#define PHP_XHPROF_API __declspec(dllexport)
#else
#define PHP_XHPROF_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(web3tracer);
PHP_MSHUTDOWN_FUNCTION(web3tracer);
PHP_RINIT_FUNCTION(web3tracer);
PHP_RSHUTDOWN_FUNCTION(web3tracer);
PHP_MINFO_FUNCTION(web3tracer);

PHP_FUNCTION(web3tracer_enable);
PHP_FUNCTION(web3tracer_disable);
PHP_FUNCTION(web3tracer_tag);
PHP_FUNCTION(web3tracer_endTag);

/**
 * **********************
 * GLOBAL MACRO CONSTANTS
 * **********************
 */

#define WEB3TRACER_OK_VAL								0
#define WEB3TRACER_ERROR_NESTING_VAL					1	// Code for nesting error
#define WEB3TRACER_ERROR_NOT_ENABLED_VAL				2	// Call to web3tracer_disable but tracer is not enabled
#define WEB3TRACER_ERROR_UNKNOWN_OUTPUT_FORMAT_VAL		3	// Unknown format output passed to web3tracer_disable()
#define WEB3TRACER_ERROR_WRITE_VAL						4	// File write error
 
#define WEB3TRACER_OUTPUT_XT_VAL			1	// XT output format
#define WEB3TRACER_OUTPUT_PROCESSED_VAL		3	// Native associative array format
 
#define WEB3TRACER_CALL_LIST_INCREMENT	100000		// The call list is allocated in chunks this many records big
#define WEB3TRACER_OUTPUT_INCREMENT		4194304		// 4MB
#define WEB3TRACER_LINE_BUFFER			4096		// 4KB
#define WEB3TRACER_FNAME_BUFFER			1024		// 1KB
 
/* web3tracer version                           */
#define WEB3TRACER_VERSION       "2.3.0"

/* Fictitious function name to represent top of the call tree. The paranthesis
 * in the name is to ensure we don't conflict with user function names.  */
#define WEB3TRACER_ROOT_SYMBOL                "{main}"

/* Size of a temp scratch buffer            */
#define SCRATCH_BUF_LEN            512

#if !defined(uint64)
typedef unsigned long long uint64;
#endif
#if !defined(uint32)
typedef unsigned int uint32;
#endif
#if !defined(uint8)
typedef unsigned char uint8;
#endif

#ifdef ZTS
#define WEB3TRACER_G(v) TSRMG(web3tracer_globals_id, zend_web3tracer_globals *, v)
#else
#define WEB3TRACER_G(v) (web3tracer_globals.v)
#endif

/**
 * *****************************
 * GLOBAL DATATYPES AND TYPEDEFS
 * *****************************
 */

typedef struct web3tracer_cg_cycledelta_t {
	uint64							amount;
} web3tracer_cg_cycledelta_t;
 
typedef struct web3tracer_cg_cycle_t {
	char							*name;
	web3tracer_cg_cycledelta_t		time;
	web3tracer_cg_cycledelta_t		mmax;
	web3tracer_cg_cycledelta_t		mall;
	web3tracer_cg_cycledelta_t		mfre;
	web3tracer_cg_cycledelta_t		mvar;
	struct web3tracer_cg_cycle_t	*prev;
} web3tracer_cg_cycle_t;

typedef struct web3tracer_delta_t {
	uint64							start;
	uint64							delta;
	uint64							deltaC;
	uint64							childAmounts;
} web3tracer_delta_t;

typedef struct web3tracer_cg_call_t {
	int								drop;
	char							*fn;
	web3tracer_delta_t				time;
	web3tracer_delta_t				mmax;
	web3tracer_delta_t				mall;
	web3tracer_delta_t				mfre;
	web3tracer_delta_t				mvar;
	struct web3tracer_cg_call_t		*cycleSource;
	struct web3tracer_cg_call_t		*firstCycleSource;
	struct web3tracer_cg_cycle_t	*cycle;
	struct web3tracer_cg_call_t		*prev;
	struct web3tracer_cg_call_t		*next;
} web3tracer_cg_call_t;

typedef struct web3tracer_cg_proc_t {
	uint64							amount;
	uint64							internalAmount;
	uint64							totalAmount;
	uint64							totalAmountSub;
} web3tracer_cg_proc_t;
 
 typedef struct {
	uint32							call_no;
	uint32							tagNo;
} web3tracer_entry_t; 

typedef struct {
	int								in_out;
	uint64							time;
	uint64							mmax;
	uint64							mem;
} web3tracer_big_entry_t; 


ZEND_BEGIN_MODULE_GLOBALS(web3tracer)
	/* web3tracer state */
	int             	enabled;
	int					opt_separateCompileFunc;
	char				*reqNewTag;
	int					reqEndTag;
	web3tracer_entry_t	lastInEntry,
						parentInEntry,
						*crtParent;
	
	/*	Process variables */
	web3tracer_cg_cycle_t		*lastCycle;
	web3tracer_cg_call_t		*lastCall;
	char						fname[WEB3TRACER_FNAME_BUFFER],
								fname2[WEB3TRACER_FNAME_BUFFER];
	int							level,
								have_nesting_error,
								have_nesting_error_pending;
	uint64						startTime,
								adjustTime;
	
	
	/* Output variable */
	zval	*z_out;

	/* This array is used to store cpu frequencies for all available logical
	 * cpus.  For now, we assume the cpu frequencies will not change for power
	 * saving or other reasons. If we need to worry about that in the future, we
	 * can use a periodical timer to re-calculate this arrary every once in a
	 * while (for example, every 1 or 5 seconds). */
	double *cpu_frequencies;

	/* The number of logical CPUs this machine has. */
	uint32 cpu_num;

	/* The saved cpu affinity. */
	cpu_set_t prev_mask;

	/* The cpu id current process is bound to. (default 0) */
	uint32 cur_cpu_id;
	uint64						init_time;
	uint32					call_no;

#if PHP_VERSION_ID >= 50500
	void (*_zend_execute_ex)(zend_execute_data *execute_data TSRMLS_DC);
	void (*_zend_execute_internal)(zend_execute_data *execute_data_ptr, zend_fcall_info *fci, int return_value_used TSRMLS_DC);
#else
	void (*_zend_execute)(zend_op_array *op_array TSRMLS_DC);
	void (*_zend_execute_internal) (zend_execute_data *execute_data_ptr, int return_value_used TSRMLS_DC);
#endif	
	/* Pointer to the original compile function */
	zend_op_array * (*_zend_compile_file) (zend_file_handle *file_handle,
							int type TSRMLS_DC);
	zend_op_array* (*_zend_compile_string) (zval *source_string,
							char *filename TSRMLS_DC);
ZEND_END_MODULE_GLOBALS(web3tracer)

#endif	/* PHP_WEB3TRACER_H */
