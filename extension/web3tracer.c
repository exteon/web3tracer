/*
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

#include "php_web3tracer.h"

/**
 * ***********************
 * GLOBAL STATIC VARIABLES
 * ***********************
 */
 
ZEND_DECLARE_MODULE_GLOBALS(web3tracer)

/**
 * ****************************
 * STATIC FUNCTION DECLARATIONS
 * ****************************
 */
static void web3tracer_stop(TSRMLS_D);
static void web3tracer_end(TSRMLS_D);
static void web3tracer_free(TSRMLS_D);

static void web3tracer_add_in(uint32 call_no,const char *func_name,const char *class_name, int internal_user,const char *include_file,uint line_no TSRMLS_DC);
static void web3tracer_add_out(uint32 call_no TSRMLS_DC);

static inline uint64 cycle_timer(TSRMLS_D);
static double get_cpu_frequency(TSRMLS_D);
static void clear_frequencies(TSRMLS_D);
static void get_all_cpu_frequencies(TSRMLS_D);
static long get_us_interval(struct timeval *start, struct timeval *end TSRMLS_DC);
static inline double get_us_from_tsc(uint64 count, double cpu_frequency TSRMLS_DC);
static zval * web3tracer_hash_lookup(zval *arr, char *symbol, zval *init TSRMLS_DC);
static void web3tracer_procTag3(TSRMLS_D);

void web3tracer_execute_ex(zend_execute_data *execute_data TSRMLS_DC);
void web3tracer_execute_internal (zend_execute_data *execute_data, zval *return_value TSRMLS_DC);

zend_op_array* web3tracer_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC);
zend_op_array* web3tracer_compile_string(zval *source_string, char *filename TSRMLS_DC);

/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO(arginfo_web3tracer_enable, 0)
	ZEND_ARG_INFO(0,options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_web3tracer_disable, 0)
	ZEND_ARG_INFO(0,output)
	ZEND_ARG_INFO(0,filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_web3tracer_tag, 0)
	ZEND_ARG_INFO(0,tagName)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_web3tracer_endTag, 0)
ZEND_END_ARG_INFO()

/**
 * *********************
 * PHP EXTENSION GLOBALS
 * *********************
 */
/* List of functions implemented/exposed by web3tracer */
zend_function_entry web3tracer_functions[] = {
  PHP_FE(web3tracer_enable, arginfo_web3tracer_enable)
  PHP_FE(web3tracer_disable, arginfo_web3tracer_disable)
  PHP_FE(web3tracer_tag, arginfo_web3tracer_tag)
  PHP_FE(web3tracer_endTag, arginfo_web3tracer_endTag)
  {NULL, NULL, NULL}
};

/* Callback functions for the web3tracer extension */
zend_module_entry web3tracer_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
  STANDARD_MODULE_HEADER,
#endif
  "web3tracer",                        /* Name of the extension */
  web3tracer_functions,                /* List of functions exposed */
  PHP_MINIT(web3tracer),               /* Module init callback */
  PHP_MSHUTDOWN(web3tracer),           /* Module shutdown callback */
  PHP_RINIT(web3tracer),               /* Request init callback */
  PHP_RSHUTDOWN(web3tracer),           /* Request shutdown callback */
  PHP_MINFO(web3tracer),               /* Module info callback */
#if ZEND_MODULE_API_NO >= 20010901
  WEB3TRACER_VERSION,
#endif
  STANDARD_MODULE_PROPERTIES
};

/* Init module */
ZEND_GET_MODULE(web3tracer)


/**
 * **********************************
 * PHP EXTENSION FUNCTION DEFINITIONS
 * **********************************
 */

/**
 * Start XHProf profiling in hierarchical mode.
 *
 * @param  long $flags  flags for hierarchical mode
 * @return void
 * @author kannan
 */
PHP_FUNCTION(web3tracer_enable) {
	zval	*options = 0;
	zval	*crtOpt;
	if (!WEB3TRACER_G(enabled)) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|a", &options) == FAILURE) {
			return;
		}
		if(options){
			crtOpt=web3tracer_hash_lookup(options,"separateCompileFunc",0 TSRMLS_CC);
			if(crtOpt){
				convert_to_boolean_ex(crtOpt);
				WEB3TRACER_G(opt_separateCompileFunc)=Z_LVAL_P(crtOpt);
			}
		}
		
		WEB3TRACER_G(enabled)      					=1;
		WEB3TRACER_G(call_no)						=0;
		WEB3TRACER_G(reqNewTag)						=0;
		WEB3TRACER_G(reqEndTag)						=0;
		WEB3TRACER_G(parentInEntry).tagNo			=0;
		WEB3TRACER_G(level)							=0;
		WEB3TRACER_G(have_nesting_error)			=0;
		WEB3TRACER_G(have_nesting_error_pending)	=0;
		WEB3TRACER_G(lastCall)						=NULL;

		/* NOTE(cjiang): some fields such as cpu_frequencies take relatively longer
		 * to initialize, (5 milisecond per logical cpu right now), therefore we
		 * calculate them lazily. */
		if (WEB3TRACER_G(cpu_frequencies) == NULL) {
			get_all_cpu_frequencies(TSRMLS_C);
			restore_cpu_affinity(&WEB3TRACER_G(prev_mask) TSRMLS_CC);
		}

		/* bind to a random cpu so that we can use rdtsc instruction. */
		bind_to_cpu((int) (rand() % WEB3TRACER_G(cpu_num)) TSRMLS_CC);
		
		array_init(&WEB3TRACER_G(z_out));

		WEB3TRACER_G(startTime)=cycle_timer();
		WEB3TRACER_G(adjustTime)=0;
		web3tracer_add_in(0, WEB3TRACER_ROOT_SYMBOL, NULL, 0, NULL, 0 TSRMLS_CC);
		WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
	}
}

/**
 * Stops XHProf from profiling in hierarchical mode anymore and returns the
 * profile info.
 *
 * @param  void
 * @return array  hash-array of XHProf's profile info
 * @author kannan, hzhao
 */
PHP_FUNCTION(web3tracer_disable) {
	char						*filename;
	size_t						filename_len;
	long						output;
	uint32						i;
	zval						*ret;
	
	WEB3TRACER_G(startTime)=cycle_timer();
	if(!WEB3TRACER_G(enabled))
		RETURN_LONG(WEB3TRACER_ERROR_NOT_ENABLED_VAL);
	WEB3TRACER_G(enabled)=0;	
	web3tracer_stop(TSRMLS_C);
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"l|s", &output, &filename, &filename_len) == FAILURE) {
		web3tracer_free(TSRMLS_C);
		return;
	}
	web3tracer_procTag3(TSRMLS_C);
	if(WEB3TRACER_G(level)!=0){
		WEB3TRACER_G(have_nesting_error)=1;
	}
	if(WEB3TRACER_G(have_nesting_error)){
		web3tracer_free(TSRMLS_C);
		RETURN_LONG(WEB3TRACER_ERROR_NESTING_VAL);
	}
	
	switch(output){
		case WEB3TRACER_OUTPUT_PROCESSED_VAL:
			web3tracer_free(TSRMLS_C);
			RETURN_ZVAL(&WEB3TRACER_G(z_out),0,1);
			break;
		default:
			web3tracer_free(TSRMLS_C);
			RETURN_LONG(WEB3TRACER_ERROR_UNKNOWN_OUTPUT_FORMAT_VAL);
	}
}

PHP_FUNCTION(web3tracer_tag){
	char 	*tagName;
	size_t	tagNameLen;
	if(WEB3TRACER_G(enabled)){
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s", &tagName, &tagNameLen) != SUCCESS) {
			return;
		}
		WEB3TRACER_G(reqNewTag)=strdup(tagName);
	}
}

PHP_FUNCTION(web3tracer_endTag){
	if(WEB3TRACER_G(enabled)){
		WEB3TRACER_G(reqEndTag)=1;
	}
}

/**
 * Module init callback.
 *
 * @author cjiang
 */
PHP_MINIT_FUNCTION(web3tracer) {
	REGISTER_LONG_CONSTANT("WEB3TRACER_OK", WEB3TRACER_OK_VAL, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("WEB3TRACER_ERROR_NESTING", WEB3TRACER_ERROR_NESTING_VAL, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("WEB3TRACER_ERROR_NOT_ENABLED", WEB3TRACER_ERROR_NOT_ENABLED_VAL, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("WEB3TRACER_ERROR_UNKNOWN_OUTPUT_FORMAT", WEB3TRACER_ERROR_UNKNOWN_OUTPUT_FORMAT_VAL, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("WEB3TRACER_ERROR_WRITE", WEB3TRACER_ERROR_WRITE_VAL, CONST_CS | CONST_PERSISTENT);

	REGISTER_LONG_CONSTANT("WEB3TRACER_OUTPUT_PROCESSED", WEB3TRACER_OUTPUT_PROCESSED_VAL, CONST_CS | CONST_PERSISTENT);

	/* Replace zend_compile with our proxy */
	WEB3TRACER_G(_zend_compile_file) = zend_compile_file;
	zend_compile_file  = web3tracer_compile_file;

	/* Replace zend_compile_string with our proxy */
	WEB3TRACER_G(_zend_compile_string) = zend_compile_string;
	zend_compile_string = web3tracer_compile_string;

	/* Replace zend_execute with our proxy */

    WEB3TRACER_G(_zend_execute_ex) = zend_execute_ex;
	zend_execute_ex = web3tracer_execute_ex;

	/* Replace zend_execute_internal with our proxy */
	WEB3TRACER_G(_zend_execute_internal) = zend_execute_internal;
	zend_execute_internal = web3tracer_execute_internal;

	return SUCCESS;
}

/**
 * Module shutdown callback.
 */
PHP_MSHUTDOWN_FUNCTION(web3tracer) {
	/* Remove proxies, restore the originals */
	zend_execute_ex     = WEB3TRACER_G(_zend_execute_ex);
	zend_execute_internal = WEB3TRACER_G(_zend_execute_internal);
	zend_compile_file     = WEB3TRACER_G(_zend_compile_file);
	zend_compile_string = WEB3TRACER_G(_zend_compile_string);
	return SUCCESS;
}

/**
 * Request init callback.
 *
 * @author Constantin-Emil Marina
 */
 PHP_RINIT_FUNCTION(web3tracer) {
	WEB3TRACER_G(enabled)=0;
	WEB3TRACER_G(init_time)=cycle_timer();

	/* Get the number of available logical CPUs. */
	WEB3TRACER_G(cpu_num) = sysconf(_SC_NPROCESSORS_CONF);

  /* Get the cpu affinity mask. */
#ifndef __APPLE__
	if (GET_AFFINITY(0, sizeof(cpu_set_t), &WEB3TRACER_G(prev_mask)) < 0) {
		perror("getaffinity");
		return FAILURE;
	}
#else
	CPU_ZERO(&(WEB3TRACER_G(prev_mask)));
#endif

	/* Initialize cpu_frequencies and cur_cpu_id. */
	WEB3TRACER_G(cpu_frequencies) = NULL;
	WEB3TRACER_G(cur_cpu_id) = 0;

#if defined(DEBUG)
	/* To make it random number generator repeatable to ease testing. */
	srand(0);
#endif
	return SUCCESS;
}

/**
 * Request shutdown callback. Stop profiling and return.
 */
PHP_RSHUTDOWN_FUNCTION(web3tracer) {
	if (WEB3TRACER_G(enabled)) {
		web3tracer_stop(TSRMLS_C);
		web3tracer_free(TSRMLS_C);
	}
	/* Make sure cpu_frequencies is free'ed. */
	clear_frequencies(TSRMLS_C);
	return SUCCESS;
}

/**
 * Module info callback. Returns the web3tracer version.
 */
PHP_MINFO_FUNCTION(web3tracer TSRMLS_DC)
{
  char buf[SCRATCH_BUF_LEN];
  char tmp[SCRATCH_BUF_LEN];
  int i;
  int len;

  php_info_print_table_start();
  php_info_print_table_header(2, "web3tracer", WEB3TRACER_VERSION);
  len = snprintf(buf, SCRATCH_BUF_LEN, "%d", WEB3TRACER_G(cpu_num));
  buf[len] = 0;
  php_info_print_table_header(2, "CPU num", buf);

  if (WEB3TRACER_G(cpu_frequencies)) {
    /* Print available cpu frequencies here. */
    php_info_print_table_header(2, "CPU logical id", " Clock Rate (MHz) ");
    for (i = 0; i < WEB3TRACER_G(cpu_num); ++i) {
      len = snprintf(buf, SCRATCH_BUF_LEN, " CPU %d ", i);
      buf[len] = 0;
      len = snprintf(tmp, SCRATCH_BUF_LEN, "%f", WEB3TRACER_G(cpu_frequencies)[i]);
      tmp[len] = 0;
      php_info_print_table_row(2, buf, tmp);
    }
  }

  php_info_print_table_end();
}

#define web3tracer_cg_start(prop,amt)		\
	newCall->prop.start=amt;		\
	newCall->prop.delta=0;		\
	newCall->prop.deltaC=0;		\
	newCall->prop.childAmounts=0;		\

#define web3tracer_cg_stop(prop,amt)		\
	someCall=WEB3TRACER_G(lastCall)->prev;		\
	while(		\
		someCall &&		\
		someCall->drop		\
	){		\
		someCall=someCall->prev;		\
	}		\
	prop.amount=amt;		\
	if(someCall){		\
		someCall->prop.childAmounts+=prop.amount;		\
	}		\
	prop.internalAmount=prop.amount-WEB3TRACER_G(lastCall)->prop.childAmounts;		\
	prop.totalAmountSub=prop.amount-WEB3TRACER_G(lastCall)->prop.delta;		\
	prop.totalAmount=prop.totalAmountSub-WEB3TRACER_G(lastCall)->prop.deltaC;		\
	if(WEB3TRACER_G(lastCall)->cycleSource!=WEB3TRACER_G(lastCall)){		\
		WEB3TRACER_G(lastCall)->cycleSource->prop.deltaC+=prop.totalAmountSub;		\
		for(		\
			callCursor=WEB3TRACER_G(lastCall)->cycleSource->next;		\
			callCursor!=WEB3TRACER_G(lastCall);		\
			callCursor=callCursor->next		\
		){		\
			if(!callCursor->drop){		\
				callCursor->prop.delta+=prop.totalAmountSub;		\
			}		\
		}		\
	}		\
	if(WEB3TRACER_G(lastCall)->cycle)		\
		WEB3TRACER_G(lastCall)->cycle->prop.amount+=prop.totalAmount;
	

/**
 * ***************************************************
 * COMMON HELPER FUNCTION DEFINITIONS AND LOCAL MACROS
 * ***************************************************
 */

/**
 * Looksup the hash table for the given symbol
 * Initializes a new array() if symbol is not present
 *
 * @author kannan, veeve, Constantin-Emil MARINA
 */
static zval * web3tracer_hash_lookup(zval *arr, char *symbol, zval *init TSRMLS_DC) {
	HashTable   *ht;
	void        *data;
	zval        *found,
				new;

	/* Bail if something is goofy */
	if (!arr || !(ht = HASH_OF(arr))) {
		return (zval *) 0;
	}
	
	/* Lookup our hash table */
	zend_string *key_str = zend_string_init(symbol, strlen(symbol), 0);
    found = zend_hash_find(ht, key_str);
    zend_string_release(key_str);
	if(!found){
		if (init) {
			/* Add symbol to hash table */
			array_init(init);
			add_assoc_zval(arr, symbol, init);
			return init;
		}
	}

	return found;
}

/**
 * Add (or initialize) a double value in an array key
 *
 * @author Constantin-Emil MARINA
 */
static void web3tracer_hash_add(zval *arr, char *symbol, uint64 val  TSRMLS_DC) {
	HashTable   *ht;
	ht = HASH_OF(arr);

	/* Lookup our hash table */
	zend_string *key_str = zend_string_init(symbol, strlen(symbol), 0);
    zval *counts = zend_hash_find(ht, key_str);
    zend_string_release(key_str);
	if(counts){
		Z_DVAL_P(counts)+=(double)val;
	} else {
		add_assoc_double(arr, symbol, (double) val);
	}
}


 static void web3tracer_add_output_internal(char *fname, uint64 time, uint64 mmax, uint64 mall, uint64 mfre, uint64 mvar TSRMLS_DC){
	zval	*z_entry,
			funcEntry,
			statEntry;
	
	z_entry=web3tracer_hash_lookup(&WEB3TRACER_G(z_out),fname,&funcEntry TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,"stats",&statEntry TSRMLS_CC);
	web3tracer_hash_add(z_entry,"time",time TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mmax",mmax TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mall",mall TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mfre",mfre TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mvar",mvar TSRMLS_CC);
	web3tracer_hash_add(z_entry,"calls",1 TSRMLS_CC);
	
}

 static void web3tracer_add_output_call(char *caller, char* callee, uint64 time, uint64 mmax, uint64 mall, uint64 mfre, uint64 mvar TSRMLS_DC){
	zval	*z_entry,
			callerEntry,
			calleesEntry,
			calleeEntry,
			statsEntry;
	
	z_entry=web3tracer_hash_lookup(&WEB3TRACER_G(z_out),caller,&callerEntry TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,"callees",&calleesEntry TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,callee,&calleeEntry TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,"stats",&statsEntry TSRMLS_CC);
	web3tracer_hash_add(z_entry,"time",time TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mmax",mmax TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mall",mall TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mfre",mfre TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mvar",mvar TSRMLS_CC);
	web3tracer_hash_add(z_entry,"calls",1 TSRMLS_CC);
}
 
/**
 * Add function begin entry.
 *
 * @author Constantin-Emil Marina
 */
static void web3tracer_add_in(uint32 call_no, const char *func_name, const char *class_name, int internal_user, const char *include_file, uint line_no TSRMLS_DC){
	web3tracer_cg_cycle_t	*newCycle;
	web3tracer_cg_call_t	*newCall,
							*callCursor;
	const char				*fnamep;
	web3tracer_big_entry_t	entry;
	
	if(WEB3TRACER_G(have_nesting_error)){
		return;
	}
	int i;
	WEB3TRACER_G(level)++;
	if(WEB3TRACER_G(have_nesting_error_pending)){
		WEB3TRACER_G(have_nesting_error)=1;
	}
	if(WEB3TRACER_G(have_nesting_error)){
		return;
	}
	
	WEB3TRACER_G(lastInEntry).call_no	=call_no;
	WEB3TRACER_G(lastInEntry).tagNo		=0;
	entry.in_out				=0;
	entry.time					=WEB3TRACER_G(startTime)-WEB3TRACER_G(adjustTime);
	entry.mmax					=zend_memory_peak_usage(0);
	entry.mem					=zend_memory_usage(0);
	
	if(WEB3TRACER_G(lastCall)&&WEB3TRACER_G(lastCall)->next){
		newCall=WEB3TRACER_G(lastCall)->next;
	} else {
		newCall=malloc(sizeof(web3tracer_cg_call_t));
		newCall->fn=NULL;
		if(WEB3TRACER_G(lastCall))
			WEB3TRACER_G(lastCall)->next=newCall;
		newCall->prev=WEB3TRACER_G(lastCall);
		newCall->next=NULL;
	}
	newCall->cycle=0;
	newCall->cycleSource=newCall;
	newCall->firstCycleSource=newCall;
	if(
		(
			!class_name ||
			!strlen(class_name)
		) &&
		(
			strcmp(func_name,"call_user_func")==0 ||
			strcmp(func_name,"call_user_func_array")==0
		)
	){
		newCall->drop=1;
	} else {
		newCall->drop=0;
		if(
			class_name &&
			strlen(class_name)
		){
			snprintf(WEB3TRACER_G(fname),WEB3TRACER_FNAME_BUFFER,"%s::%s",class_name,func_name);
			fnamep=WEB3TRACER_G(fname);
		} else {
			if(include_file){
				if(line_no){
					snprintf(WEB3TRACER_G(fname),WEB3TRACER_FNAME_BUFFER,"%s(%s:%d)",func_name,include_file,line_no);
				} else {
					snprintf(WEB3TRACER_G(fname),WEB3TRACER_FNAME_BUFFER,"%s(%s)",func_name,include_file);
				}
				fnamep=WEB3TRACER_G(fname);
			} else {
				fnamep=func_name;
			}
		}
		newCall->fn=strdup(fnamep);
		web3tracer_cg_start(time,entry.time);
		web3tracer_cg_start(mmax,entry.mmax);
		web3tracer_cg_start(mall,entry.mem);
		web3tracer_cg_start(mfre,entry.mem);
		web3tracer_cg_start(mvar,entry.mem);
		for(callCursor=newCall->prev;callCursor;callCursor=callCursor->firstCycleSource->prev){
			if(
				!callCursor->drop &&
				strcmp(newCall->fn,callCursor->fn)==0
			){
				newCall->cycleSource=callCursor;
				if(callCursor->cycle){
					newCall->cycle=callCursor->cycle;
					newCall->firstCycleSource=callCursor->firstCycleSource;
				} else {
					newCall->firstCycleSource=callCursor;
					newCycle=malloc(sizeof(web3tracer_cg_cycle_t));
					snprintf(WEB3TRACER_G(fname),WEB3TRACER_FNAME_BUFFER,"<CYCLE %s>",newCall->fn);
					newCycle->name=strdup(WEB3TRACER_G(fname));
					newCycle->time.amount=0;
					newCycle->mmax.amount=0;
					newCycle->mall.amount=0;
					newCycle->mfre.amount=0;
					newCycle->mvar.amount=0;
					newCall->cycle=callCursor->cycle=newCycle;
				}
			}
		}
	}
	WEB3TRACER_G(lastCall)=newCall;
}
/**
 * Add function end entry.
 *
 * @author Constantin-Emil Marina
 */
static void web3tracer_add_out(uint32 call_no TSRMLS_DC){
	web3tracer_big_entry_t		entry;
	web3tracer_cg_call_t		*someCall,
								*callCursor;
	web3tracer_cg_proc_t		time,
								mmax,
								mall,
								mfre,
								mvar;
	int64_t						dmem,
								dhave;
							
	if(WEB3TRACER_G(have_nesting_error)){
		return;
	}
	WEB3TRACER_G(level)--;
	int i;
	if(WEB3TRACER_G(level)==0){
		WEB3TRACER_G(have_nesting_error_pending)=1;
	}
	if(WEB3TRACER_G(level)<0){
		WEB3TRACER_G(have_nesting_error)=1;
	}
	if(WEB3TRACER_G(have_nesting_error)){
		return;
	}
							
	entry.time			=WEB3TRACER_G(startTime)-WEB3TRACER_G(adjustTime);
	entry.mmax			=zend_memory_peak_usage(0);
	entry.mem			=zend_memory_usage(0);
	
	if(!WEB3TRACER_G(lastCall)->drop){
		web3tracer_cg_stop(time,entry.time-WEB3TRACER_G(lastCall)->time.start);
		web3tracer_cg_stop(mmax,entry.mmax-WEB3TRACER_G(lastCall)->mmax.start);
		dmem=entry.mem-WEB3TRACER_G(lastCall)->mvar.start;
		dhave=WEB3TRACER_G(lastCall)->mall.childAmounts-WEB3TRACER_G(lastCall)->mfre.childAmounts;
		if(dmem>=dhave){
			web3tracer_cg_stop(mall,WEB3TRACER_G(lastCall)->mfre.childAmounts+dmem);
			web3tracer_cg_stop(mfre,WEB3TRACER_G(lastCall)->mfre.childAmounts);
			web3tracer_cg_stop(mvar,2*WEB3TRACER_G(lastCall)->mfre.childAmounts+dmem);
		} else {
			web3tracer_cg_stop(mall,WEB3TRACER_G(lastCall)->mall.childAmounts);
			web3tracer_cg_stop(mfre,WEB3TRACER_G(lastCall)->mall.childAmounts-dmem);
			web3tracer_cg_stop(mvar,2*WEB3TRACER_G(lastCall)->mall.childAmounts-dmem);
		}
		web3tracer_add_output_internal(
			WEB3TRACER_G(lastCall)->fn,
			(uint64)(1000*get_us_from_tsc(time.internalAmount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)]) TSRMLS_CC),
//			0,
			mmax.internalAmount,
			mall.internalAmount,
			mfre.internalAmount,
			mvar.internalAmount
		);
		if(WEB3TRACER_G(lastCall)->cycle){
			web3tracer_add_output_call(WEB3TRACER_G(lastCall)->cycle->name,WEB3TRACER_G(lastCall)->fn,(uint64)(1000*get_us_from_tsc(time.totalAmount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])),mmax.totalAmount,mall.totalAmount,mfre.totalAmount,mvar.totalAmount TSRMLS_CC);
			if(WEB3TRACER_G(lastCall)->cycleSource==WEB3TRACER_G(lastCall)&&WEB3TRACER_G(lastCall)->prev){
				someCall=WEB3TRACER_G(lastCall)->prev;
				while(
					someCall &&
					someCall->drop
				){
					someCall=someCall->prev;
				}
				if(someCall){
					web3tracer_add_output_call(someCall->fn,WEB3TRACER_G(lastCall)->cycle->name,(uint64)(1000*get_us_from_tsc(WEB3TRACER_G(lastCall)->cycle->time.amount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])),WEB3TRACER_G(lastCall)->cycle->mmax.amount,WEB3TRACER_G(lastCall)->cycle->mall.amount,WEB3TRACER_G(lastCall)->cycle->mfre.amount,WEB3TRACER_G(lastCall)->cycle->mvar.amount TSRMLS_CC);
				}
				WEB3TRACER_G(lastCall)->cycle->time.amount=0;
				WEB3TRACER_G(lastCall)->cycle->mmax.amount=0;
				WEB3TRACER_G(lastCall)->cycle->mall.amount=0;
				WEB3TRACER_G(lastCall)->cycle->mfre.amount=0;
				WEB3TRACER_G(lastCall)->cycle->mvar.amount=0;
			}
		} else {
			someCall=WEB3TRACER_G(lastCall)->prev;
			while(
				someCall &&
				someCall->drop
			){
				someCall=someCall->prev;
			}
			if(someCall){
				web3tracer_add_output_call(someCall->fn,WEB3TRACER_G(lastCall)->fn,(uint64)(1000*get_us_from_tsc(time.totalAmount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])),mmax.totalAmount,mall.totalAmount,mfre.totalAmount,mvar.totalAmount TSRMLS_CC);
			}
		}
		free(WEB3TRACER_G(lastCall)->fn);
		WEB3TRACER_G(lastCall)->fn=NULL;
	}
	if(
		WEB3TRACER_G(lastCall)->cycle &&
		WEB3TRACER_G(lastCall)->firstCycleSource==WEB3TRACER_G(lastCall)
	){
		free(WEB3TRACER_G(lastCall)->cycle->name);
		free(WEB3TRACER_G(lastCall)->cycle);
	}
	WEB3TRACER_G(lastCall)=WEB3TRACER_G(lastCall)->prev;
}

/**
 * Convert from TSC counter values to equivalent microseconds.
 *
 * @param uint64 count, TSC count value
 * @param double cpu_frequency, the CPU clock rate (MHz)
 * @return 64 bit unsigned integer
 *
 * @author cjiang
 */
static inline double get_us_from_tsc(uint64 count, double cpu_frequency) {
  return count / cpu_frequency;
}
static const char *web3tracer_get_active_function_name_safe(TSRMLS_D)
{
	zend_function *func;

	if (!zend_is_executing()) {
		return NULL;
	}
	func = EG(current_execute_data)->func;
	switch (func->type) {
		case ZEND_USER_FUNCTION: {
				zend_string *function_name = func->common.function_name;

				if (function_name) {
					return ZSTR_VAL(function_name);
				} else {
					return NULL;
				}
			}
			break;
		case ZEND_INTERNAL_FUNCTION:
			if(func->common.function_name){
				return ZSTR_VAL(func->common.function_name);
			}
			return "?";
			break;
		default:
			return NULL;
	}
}
/**
 * Add function begin entry, with formation of function name.
 * Largely copied from main/main.c#php_verror
 * @author Constantin-Emil Marina
 */
static int web3tracer_in(int internal_user TSRMLS_DC) {
	const char	*function=NULL;
	const char	*include_file=NULL;
	const char	*class_name=NULL;
	uint line_no=0;

	if(ZEND_CALL_INFO(EG(current_execute_data)) & ZEND_CALL_CLOSURE){
		include_file=ZSTR_VAL(EG(current_execute_data)->func->op_array.filename);
		function="(closure)";
		line_no=EG(current_execute_data)->func->op_array.line_start;
	}
	else if (
		EG(current_execute_data) &&
		ZEND_USER_CODE(EG(current_execute_data)->func->type) &&
		EG(current_execute_data)->opline &&
		EG(current_execute_data)->opline->opcode == ZEND_INCLUDE_OR_EVAL
	) {
		switch (EG(current_execute_data)->opline->extended_value) {
			case ZEND_EVAL:
				function = "eval";
				break;
			case ZEND_INCLUDE:
				function = "include";
				include_file=zend_get_executed_filename(TSRMLS_C);
				break;
			case ZEND_INCLUDE_ONCE:
				function = "include_once";
				include_file=zend_get_executed_filename(TSRMLS_C);
				break;
			case ZEND_REQUIRE:
				function = "require";
				include_file=zend_get_executed_filename(TSRMLS_C);
				break;
			case ZEND_REQUIRE_ONCE:
				function = "require_once";
				include_file=zend_get_executed_filename(TSRMLS_C);
				break;
			default:
				function = "ZEND_INCLUDE_OR_EVAL";
		}
	} else {
		function = web3tracer_get_active_function_name_safe(TSRMLS_C);
		if (
			!function ||
			!strlen(function)
		) {
			function=WEB3TRACER_ROOT_SYMBOL;
			include_file=zend_get_executed_filename(TSRMLS_C);
		} else {
			class_name = get_active_class_name(NULL TSRMLS_CC);
		}
	}

	web3tracer_add_in(
		++WEB3TRACER_G(call_no),
		function,
		class_name,
		internal_user,
		include_file,
		line_no
		TSRMLS_CC
	);
	return 1;
}
static void web3tracer_out(uint32 call_no TSRMLS_DC){
	web3tracer_add_out(call_no TSRMLS_CC);
}
static void web3tracer_procTag1(TSRMLS_D){
	if(
		WEB3TRACER_G(parentInEntry).tagNo
	){
		web3tracer_out(WEB3TRACER_G(parentInEntry).tagNo TSRMLS_CC);
	}
}

static void web3tracer_procTag2(TSRMLS_D){
	if(
		(
			WEB3TRACER_G(reqEndTag) ||
			WEB3TRACER_G(reqNewTag)
		) &&
		WEB3TRACER_G(parentInEntry).tagNo
	){
		web3tracer_out(WEB3TRACER_G(parentInEntry).tagNo);
		WEB3TRACER_G(parentInEntry).tagNo=0;
		WEB3TRACER_G(reqEndTag)=0;
	}
	if(WEB3TRACER_G(reqNewTag)){
		WEB3TRACER_G(parentInEntry)=WEB3TRACER_G(lastInEntry);
		web3tracer_add_in(
			++WEB3TRACER_G(call_no),
			"(tag)",
			NULL,
			1,
			WEB3TRACER_G(reqNewTag),
			0
			TSRMLS_CC
		);
		free(WEB3TRACER_G(reqNewTag));
		WEB3TRACER_G(reqNewTag)=0;
		WEB3TRACER_G(parentInEntry).tagNo=WEB3TRACER_G(call_no);
	}
}

static void web3tracer_procTag3(TSRMLS_D){
	web3tracer_add_out(WEB3TRACER_G(lastInEntry).call_no TSRMLS_CC);
	if(
		WEB3TRACER_G(parentInEntry).tagNo
	){
		web3tracer_add_out(WEB3TRACER_G(parentInEntry).tagNo TSRMLS_CC);
		WEB3TRACER_G(parentInEntry).tagNo=0;
	}
	web3tracer_add_out(0 TSRMLS_CC);
}

/**
 * ***********************
 * High precision timer related functions.
 * ***********************
 */

/**
 * Get time stamp counter (TSC) value via 'rdtsc' instruction.
 *
 * @return 64 bit unsigned integer
 * @author cjiang
 */
inline uint64 cycle_timer() {
  uint32 __a,__d;
  uint64 val;
  asm volatile("rdtsc" : "=a" (__a), "=d" (__d));
  (val) = ((uint64)__a) | (((uint64)__d)<<32);
  return val;
}

/**
 * Bind the current process to a specified CPU. This function is to ensure that
 * the OS won't schedule the process to different processors, which would make
 * values read by rdtsc unreliable.
 *
 * @param uint32 cpu_id, the id of the logical cpu to be bound to.
 * @return int, 0 on success, and -1 on failure.
 *
 * @author cjiang
 */
int bind_to_cpu(uint32 cpu_id) {
  cpu_set_t new_mask;

  CPU_ZERO(&new_mask);
  CPU_SET(cpu_id, &new_mask);

  if (SET_AFFINITY(0, sizeof(cpu_set_t), &new_mask) < 0) {
    perror("setaffinity");
    return -1;
  }

  /* record the cpu_id the process is bound to. */
  WEB3TRACER_G(cur_cpu_id) = cpu_id;

  return 0;
}

/**
 * Get time delta in microseconds.
 */
static long get_us_interval(struct timeval *start, struct timeval *end) {
  return (((end->tv_sec - start->tv_sec) * 1000000)
          + (end->tv_usec - start->tv_usec));
}

/**
 * This is a microbenchmark to get cpu frequency the process is running on. The
 * returned value is used to convert TSC counter values to microseconds.
 *
 * @return double.
 * @author cjiang
 */
static double get_cpu_frequency() {
  struct timeval start;
  struct timeval end;

  if (gettimeofday(&start, 0)) {
    perror("gettimeofday");
    return 0.0;
  }
  uint64 tsc_start = cycle_timer();
  /* Sleep for 5 miliseconds. Comparaing with gettimeofday's  few microseconds
   * execution time, this should be enough. */
  usleep(5000);
  if (gettimeofday(&end, 0)) {
    perror("gettimeofday");
    return 0.0;
  }
  uint64 tsc_end = cycle_timer();
  return (tsc_end - tsc_start) * 1.0 / (get_us_interval(&start, &end));
}

/**
 * Calculate frequencies for all available cpus.
 *
 * @author cjiang
 */
static void get_all_cpu_frequencies(TSRMLS_D) {
  int id;
  double frequency;

  WEB3TRACER_G(cpu_frequencies) = malloc(sizeof(double) * WEB3TRACER_G(cpu_num));
  if (WEB3TRACER_G(cpu_frequencies) == NULL) {
    return;
  }

  /* Iterate over all cpus found on the machine. */
  for (id = 0; id < WEB3TRACER_G(cpu_num); ++id) {
    /* Only get the previous cpu affinity mask for the first call. */
    if (bind_to_cpu(id)) {
      clear_frequencies(TSRMLS_C);
      return;
    }

    /* Make sure the current process gets scheduled to the target cpu. This
     * might not be necessary though. */
    usleep(0);

    frequency = get_cpu_frequency();
    if (frequency == 0.0) {
      clear_frequencies(TSRMLS_C);
      return;
    }
    WEB3TRACER_G(cpu_frequencies)[id] = frequency;
  }
}

/**
 * Restore cpu affinity mask to a specified value. It returns 0 on success and
 * -1 on failure.
 *
 * @param cpu_set_t * prev_mask, previous cpu affinity mask to be restored to.
 * @return int, 0 on success, and -1 on failure.
 *
 * @author cjiang
 */
int restore_cpu_affinity(cpu_set_t * prev_mask TSRMLS_DC) {
  if (SET_AFFINITY(0, sizeof(cpu_set_t), prev_mask) < 0) {
    perror("restore setaffinity");
    return -1;
  }
  /* default value ofor cur_cpu_id is 0. */
  WEB3TRACER_G(cur_cpu_id) = 0;
  return 0;
}

/**
 * Reclaim the memory allocated for cpu_frequencies.
 *
 * @author cjiang
 */
static void clear_frequencies(TSRMLS_D) {
  if (WEB3TRACER_G(cpu_frequencies)) {
    free(WEB3TRACER_G(cpu_frequencies));
    WEB3TRACER_G(cpu_frequencies) = NULL;
  }
  restore_cpu_affinity(&WEB3TRACER_G(prev_mask) TSRMLS_CC);
}


/**
 * ***************************
 * PHP EXECUTE/COMPILE PROXIES
 * ***************************
 */

/**
 * XHProf enable replaced the zend_execute function with this
 * new execute function. We can do whatever profiling we need to
 * before and after calling the actual zend_execute().
 *
 * @author hzhao, kannan, Constantin-Emil Marina 
 */

void web3tracer_execute_ex(zend_execute_data *execute_data TSRMLS_DC) {
	web3tracer_entry_t		parentEntry=WEB3TRACER_G(lastInEntry),
							parentParentEntry=WEB3TRACER_G(parentInEntry);
	int						begun;
							
	if (!WEB3TRACER_G(enabled)) {
		WEB3TRACER_G(_zend_execute_ex)(execute_data TSRMLS_CC);
		return;
	}
	WEB3TRACER_G(startTime)=cycle_timer();
	begun=web3tracer_in(1 TSRMLS_CC);
	WEB3TRACER_G(parentInEntry)=parentEntry;
	if(begun){
		WEB3TRACER_G(parentInEntry).tagNo=0;
	}
	
	uint32	now_call_no=WEB3TRACER_G(call_no);
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);

	WEB3TRACER_G(_zend_execute_ex)(execute_data TSRMLS_CC);
  
	WEB3TRACER_G(startTime)=cycle_timer();
	if(WEB3TRACER_G(enabled)){
		if(begun){
			web3tracer_procTag1(TSRMLS_C);
			web3tracer_out(now_call_no TSRMLS_CC);
		}
		WEB3TRACER_G(lastInEntry)=parentEntry;
		WEB3TRACER_G(parentInEntry)=parentParentEntry;
		web3tracer_procTag2(TSRMLS_C);
	}
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
}

/**
 * Very similar to web3tracer_execute. Proxy for zend_execute_internal().
 * Applies to zend builtin functions.
 *
 * @author hzhao, kannan, Constantin-Emil Marina
 */

void web3tracer_execute_internal (zend_execute_data *execute_data, zval *return_value TSRMLS_DC){
	web3tracer_entry_t		parentEntry=WEB3TRACER_G(lastInEntry),
							parentParentEntry=WEB3TRACER_G(parentInEntry);
	int						begun;

	if (!WEB3TRACER_G(enabled)) {
		if (!WEB3TRACER_G(_zend_execute_internal)) {
		/* no old override to begin with. so invoke the builtin's implementation  */
			execute_internal(execute_data, return_value TSRMLS_CC);
		} else {
		/* call the old override */
			WEB3TRACER_G(_zend_execute_internal)(execute_data, return_value TSRMLS_CC);
		}
		return;
	}
	WEB3TRACER_G(startTime)=cycle_timer();
	begun=web3tracer_in(0 TSRMLS_CC);
	WEB3TRACER_G(parentInEntry)=parentEntry;
	if(begun){
		WEB3TRACER_G(parentInEntry).tagNo=0;
	}

	uint32	now_call_no=WEB3TRACER_G(call_no);
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
  if (!WEB3TRACER_G(_zend_execute_internal)) {
    /* no old override to begin with. so invoke the builtin's implementation  */
	execute_internal(execute_data, return_value);
  } else {
    /* call the old override */
	WEB3TRACER_G(_zend_execute_internal)(execute_data, return_value TSRMLS_CC);
  }
	WEB3TRACER_G(startTime)=cycle_timer();
	if(WEB3TRACER_G(enabled)){
		if(begun){
			web3tracer_procTag1(TSRMLS_C);
			web3tracer_out(now_call_no TSRMLS_CC);
		}
		WEB3TRACER_G(lastInEntry)=parentEntry;
		WEB3TRACER_G(parentInEntry)=parentParentEntry;
		web3tracer_procTag2(TSRMLS_C);
	}
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
}

/**
 * Proxy for zend_compile_file(). Used to profile PHP compilation time.
 *
 * @author kannan, hzhao, Constantin-Emil Marina
 */
zend_op_array* web3tracer_compile_file(zend_file_handle *file_handle,
                                             int type TSRMLS_DC) {
	web3tracer_entry_t		parentEntry=WEB3TRACER_G(lastInEntry);
	zend_execute_data		*data;
	uint32					now_call_no;
	zend_op_array*			ret;
	
	if (!WEB3TRACER_G(enabled)) {
		return WEB3TRACER_G(_zend_compile_file)(file_handle, type TSRMLS_CC);
	}
	WEB3TRACER_G(startTime)=cycle_timer();
	data = EG(current_execute_data);
	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_add_in(
			++WEB3TRACER_G(call_no),
			"(compile)",
			NULL,
			0,
			NULL,
			0
			TSRMLS_CC
		);
	}
	web3tracer_add_in(
		++WEB3TRACER_G(call_no),
		"(compile)",
		NULL,
		0,
		file_handle->filename,
		0
		TSRMLS_CC
	);
	now_call_no=WEB3TRACER_G(call_no);
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
	ret=WEB3TRACER_G(_zend_compile_file)(file_handle, type TSRMLS_CC);
	WEB3TRACER_G(startTime)=cycle_timer();
	web3tracer_out(now_call_no TSRMLS_CC);
	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_out(now_call_no-1 TSRMLS_CC);
	}
	WEB3TRACER_G(lastInEntry)=parentEntry;
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
	return ret;
}

/**
 * Proxy for zend_compile_string(). Used to profile PHP eval compilation time.
 */
zend_op_array* web3tracer_compile_string(zval *source_string, char *filename TSRMLS_DC) {
	web3tracer_entry_t		parentEntry=WEB3TRACER_G(lastInEntry);
	zend_execute_data		*data;
	uint32					now_call_no;
	zend_op_array*			ret;

	if (!WEB3TRACER_G(enabled)) {
		return WEB3TRACER_G(_zend_compile_string)(source_string, filename TSRMLS_CC);
	}
	WEB3TRACER_G(startTime)=cycle_timer();
	data = EG(current_execute_data);
	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_add_in(
			++WEB3TRACER_G(call_no),
			"(compile)",
			NULL,
			0,
			NULL,
			0
			TSRMLS_CC
		);
	}
	web3tracer_add_in(
		++WEB3TRACER_G(call_no),
		"(compile)",
		NULL,
		0,
		filename,
		0
		TSRMLS_CC
	);
	now_call_no=WEB3TRACER_G(call_no);
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
	ret=WEB3TRACER_G(_zend_compile_string)(source_string, filename TSRMLS_CC);
	WEB3TRACER_G(startTime)=cycle_timer();
	web3tracer_out(now_call_no TSRMLS_CC);
	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_out(now_call_no-1 TSRMLS_CC);
	}
	WEB3TRACER_G(lastInEntry)=parentEntry;
	WEB3TRACER_G(adjustTime)+=cycle_timer()-WEB3TRACER_G(startTime);
	return ret;
}

/**
 * **************************
 * MAIN WEB3TRACER CALLBACKS
 * **************************
 */

/**
 * Called from web3tracer_disable(). Removes all the proxies setup by
 * web3tracer_begin() and restores the original values.
 */
static void web3tracer_stop(TSRMLS_D) {
	/* Resore cpu affinity. */
	restore_cpu_affinity(&WEB3TRACER_G(prev_mask));
}

/**
 * Free data structures
 *
 * @author Constantin-Emil Marina
 */
static void web3tracer_free(TSRMLS_D){
	web3tracer_cg_call_t	*callCursor;
	while(WEB3TRACER_G(lastCall)){
		callCursor=WEB3TRACER_G(lastCall)->prev;
		if(WEB3TRACER_G(lastCall)->fn){
			free(WEB3TRACER_G(lastCall)->fn);
		}
		free(WEB3TRACER_G(lastCall));
		WEB3TRACER_G(lastCall)=callCursor;
	};
}
