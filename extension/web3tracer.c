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

static void web3tracer_output_xt(FILE *output_file TSRMLS_DC);
static void web3tracer_process_output(TSRMLS_D);

static void web3tracer_add_in(uint32 call_no, uint64 time,const char *func_name,const char *class_name, int internal_user,char *include_file,const char *call_file, uint call_line_no, int dealloc_include_file TSRMLS_DC);
static void web3tracer_add_out(uint32 call_no, uint64 time TSRMLS_DC);
static void web3tracer_replace_out(uint32 call_no TSRMLS_DC);

static inline uint64 cycle_timer(TSRMLS_D);
static double get_cpu_frequency(TSRMLS_D);
static void clear_frequencies(TSRMLS_D);
static void get_all_cpu_frequencies(TSRMLS_D);
static long get_us_interval(struct timeval *start, struct timeval *end TSRMLS_DC);
static inline double get_us_from_tsc(uint64 count, double cpu_frequency TSRMLS_DC);
static zval * web3tracer_hash_lookup(zval *arr, char *symbol, int init TSRMLS_DC);
static void web3tracer_procTag3(TSRMLS_D);

#if PHP_VERSION_ID >= 50500
void web3tracer_execute_ex(zend_execute_data *execute_data TSRMLS_DC);
void web3tracer_execute_internal(zend_execute_data *execute_data_ptr, zend_fcall_info *fci, int return_value_used TSRMLS_DC);
#else
void web3tracer_execute(zend_op_array *op_array TSRMLS_DC);
void web3tracer_execute_internal (zend_execute_data *execute_data_ptr, int return_value_used TSRMLS_DC);
#endif

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
 * HELPER FUNCTIONS
 * **********************************
 */

/*
 * Locked memory allocation functions
 * @author Constantin-Emil Marina
 * TODO: Also cover VC
 * TODO: Handle allocation errors now that we don't rely on emalloc
 *  anymore
 */

#ifdef HAVE_MMAP
	#include <sys/mman.h>
#endif
 
#if defined HAVE_MMAP && ( defined MAP_ANONYMOUS || defined MAP_ANON )
	#ifndef MAP_ANONYMOUS
		#define MAP_ANONYMOUS MAP_ANON
	#endif
void* web3tracer_alloc_locked( size_t size ){
	void	*res;
	res=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
	if(res)
		mlock(res,size);
	return res;
}
void web3tracer_free_locked( void *ptr, size_t size ){
	munmap(ptr,size);
}
#else
void* web3tracer_alloc_locked( size_t size ){
	return malloc(size);
}
void web3tracer_free_locked( void *ptr, size_t size ){
	free(ptr);
}
#endif

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
				convert_to_boolean_ex(&crtOpt);
				WEB3TRACER_G(opt_separateCompileFunc)=Z_BVAL_P(crtOpt);
			}
		}
		
		WEB3TRACER_G(enabled)      = 1;
		WEB3TRACER_G(call_no)		=0;
		WEB3TRACER_G(reqNewTag)		=0;
		WEB3TRACER_G(reqEndTag)		=0;
		WEB3TRACER_G(lastInEntry)	=0;
		WEB3TRACER_G(parentInEntry)	=0;

		/* Replace zend_compile with our proxy */
		WEB3TRACER_G(_zend_compile_file) = zend_compile_file;
		zend_compile_file  = web3tracer_compile_file;

		/* Replace zend_compile_string with our proxy */
		WEB3TRACER_G(_zend_compile_string) = zend_compile_string;
		zend_compile_string = web3tracer_compile_string;

		/* Replace zend_execute with our proxy */

		#if PHP_VERSION_ID >= 50500
		    WEB3TRACER_G(_zend_execute_ex) = zend_execute_ex;
			zend_execute_ex = web3tracer_execute_ex;
		#else
		    WEB3TRACER_G(_zend_execute) = zend_execute;
			zend_execute  = web3tracer_execute;
		#endif
		

		/* Replace zend_execute_internal with our proxy */
		WEB3TRACER_G(_zend_execute_internal) = zend_execute_internal;
		zend_execute_internal = web3tracer_execute_internal;
	

		/* NOTE(cjiang): some fields such as cpu_frequencies take relatively longer
		 * to initialize, (5 milisecond per logical cpu right now), therefore we
		 * calculate them lazily. */
		if (WEB3TRACER_G(cpu_frequencies) == NULL) {
			get_all_cpu_frequencies(TSRMLS_C);
			restore_cpu_affinity(&WEB3TRACER_G(prev_mask) TSRMLS_CC);
		}

		/* bind to a random cpu so that we can use rdtsc instruction. */
		bind_to_cpu((int) (rand() % WEB3TRACER_G(cpu_num)) TSRMLS_CC);

		WEB3TRACER_G(first_entry_chunk)=WEB3TRACER_G(last_entry_chunk)=web3tracer_alloc_locked(sizeof(web3tracer_entry_chunk_t));
		WEB3TRACER_G(next_entry)=&(WEB3TRACER_G(first_entry_chunk)->entries[0]);

		WEB3TRACER_G(first_entry_chunk)->next=NULL;
		WEB3TRACER_G(first_entry_chunk)->len=0;
		web3tracer_add_in(0, cycle_timer(), WEB3TRACER_ROOT_SYMBOL, NULL, 0, NULL, NULL, 0, 0 TSRMLS_CC);
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
	int							filename_len,
								level=0,
								have_nesting_error=0,
								have_nesting_error_pending=0;
	long						output;
	FILE						*output_file;
	uint32						i;
	web3tracer_entry_chunk_t	*chunkCursor;
	zval						*ret;
	
	if(!WEB3TRACER_G(enabled))
		RETURN_LONG(WEB3TRACER_ERROR_NOT_ENABLED_VAL);
	WEB3TRACER_G(enabled)=0;	
	web3tracer_stop(TSRMLS_C);
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"l|s", &output, &filename, &filename_len) == FAILURE) {
		web3tracer_free(TSRMLS_C);
		return;
	}
	web3tracer_procTag3(TSRMLS_C);
	for(chunkCursor=WEB3TRACER_G(first_entry_chunk);chunkCursor;chunkCursor=chunkCursor->next){
		for(i=0;i<chunkCursor->len;i++){
			if(have_nesting_error_pending){
				have_nesting_error=1;
				break;
			}
			switch(chunkCursor->entries[i].in_out){
				case 0:
					level++;
					break;
				case 1:
					level--;
					break;
			}
			if(level<0){
				have_nesting_error=1;
				break;
			}
			if(level==0){
				have_nesting_error_pending=1;
			}
		}
	}
	if(level!=0)
		have_nesting_error=1;
	if(have_nesting_error){
		web3tracer_free(TSRMLS_C);
		RETURN_LONG(WEB3TRACER_ERROR_NESTING_VAL);
	}
	
	switch(output){
		case WEB3TRACER_OUTPUT_XT_VAL:
			output_file=fopen(filename,"w");
			if(!output_file){
				web3tracer_free(TSRMLS_C);
				RETURN_LONG(WEB3TRACER_ERROR_WRITE_VAL);
			}
			web3tracer_output_xt(output_file TSRMLS_C);
			web3tracer_free(TSRMLS_C);
			if(fclose(output_file)!=0){
				RETURN_LONG(WEB3TRACER_ERROR_WRITE_VAL);
			}
			RETURN_LONG(WEB3TRACER_OK_VAL);
			break;
		case WEB3TRACER_OUTPUT_PROCESSED_VAL:
			MAKE_STD_ZVAL(WEB3TRACER_G(z_out));
			array_init(WEB3TRACER_G(z_out));
			web3tracer_process_output();
			web3tracer_free(TSRMLS_C);
			RETURN_ZVAL(WEB3TRACER_G(z_out),0,1);
			break;
		default:
			web3tracer_free(TSRMLS_C);
			RETURN_LONG(WEB3TRACER_ERROR_UNKNOWN_OUTPUT_FORMAT_VAL);
	}
}

PHP_FUNCTION(web3tracer_tag){
	char 	*tagName;
	int		tagNameLen;
	if(WEB3TRACER_G(enabled)){
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s", &tagName, &tagNameLen) == FAILURE) {
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

	REGISTER_LONG_CONSTANT("WEB3TRACER_OUTPUT_XT", WEB3TRACER_OUTPUT_XT_VAL, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("WEB3TRACER_OUTPUT_PROCESSED", WEB3TRACER_OUTPUT_PROCESSED_VAL, CONST_CS | CONST_PERSISTENT);
	return SUCCESS;
}

/**
 * Module shutdown callback.
 */
PHP_MSHUTDOWN_FUNCTION(web3tracer) {
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
	WEB3TRACER_G(first_entry_chunk)=NULL;
	WEB3TRACER_G(last_entry_chunk)=NULL;

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
	newCall->prop.start=amt;				\
	newCall->prop.delta=0;					\
	newCall->prop.deltaC=0;					\
	newCall->prop.childAmounts=0;			\

#define web3tracer_cg_stop(prop,amt)																	\
	prop.amount=amt;																					\
	if(lastCall->prev){																					\
		lastCall->prev->prop.childAmounts+=prop.amount;													\
	}																									\
	prop.internalAmount=prop.amount-lastCall->prop.childAmounts;										\
	prop.totalAmountSub=prop.amount-lastCall->prop.delta;												\
	prop.totalAmount=prop.totalAmountSub-lastCall->prop.deltaC;											\
	if(lastCall->cycleSource!=lastCall){																\
		lastCall->cycleSource->prop.deltaC+=prop.totalAmountSub;										\
		for(callCursor=lastCall->cycleSource->next;callCursor!=lastCall;callCursor=callCursor->next){	\
			callCursor->prop.delta+=prop.totalAmountSub;												\
		}																								\
	}																									\
	if(lastCall->cycle)																					\
		lastCall->cycle->prop.amount+=prop.totalAmount;													\
	

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
static zval * web3tracer_hash_lookup(zval *arr, char *symbol, int init TSRMLS_DC) {
	HashTable   *ht;
	void        *data;
	zval        *counts = (zval *) 0;

	/* Bail if something is goofy */
	if (!arr || !(ht = HASH_OF(arr))) {
		return (zval *) 0;
	}
	
	/* Lookup our hash table */
	if (zend_hash_find(ht, symbol, strlen(symbol) + 1, &data) == SUCCESS) {
		/* Symbol already exists */
		counts = *(zval **) data;
	} else {
		if (init) {
			/* Add symbol to hash table */
			MAKE_STD_ZVAL(counts);
			array_init(counts);
			add_assoc_zval(arr, symbol, counts);
		}
	}

	return counts;
}

/**
 * Add (or initialize) a double value in an array key
 *
 * @author Constantin-Emil MARINA
 */
static void web3tracer_hash_add(zval *arr, char *symbol, uint64 val  TSRMLS_DC) {
	HashTable   *ht;
	void        *data;

	ht = HASH_OF(arr);

	/* Lookup our hash table */
	if (zend_hash_find(ht, symbol, strlen(symbol) + 1, &data) == SUCCESS) {
		/* Symbol already exists */
		Z_DVAL_PP((zval **) data)+=(double)val;
	} else {
		add_assoc_double(arr, symbol, (double) val);
	}
}


 static void web3tracer_add_output_internal(char *fname, uint64 time, uint64 mmax, uint64 mall, uint64 mfre, uint64 mvar TSRMLS_DC){
	zval	*z_entry;
	
	z_entry=web3tracer_hash_lookup(WEB3TRACER_G(z_out),fname,1 TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,"stats",1 TSRMLS_CC);
	web3tracer_hash_add(z_entry,"time",time TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mmax",mmax TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mall",mall TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mfre",mfre TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mvar",mvar TSRMLS_CC);
	web3tracer_hash_add(z_entry,"calls",1 TSRMLS_CC);
}

 static void web3tracer_add_output_call(char *caller, char* callee, uint64 time, uint64 mmax, uint64 mall, uint64 mfre, uint64 mvar TSRMLS_DC){
	zval	*z_entry;
	
	z_entry=web3tracer_hash_lookup(WEB3TRACER_G(z_out),caller,1 TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,"callees",1 TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,callee,1 TSRMLS_CC);
	z_entry=web3tracer_hash_lookup(z_entry,"stats",1 TSRMLS_CC);
	web3tracer_hash_add(z_entry,"time",time TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mmax",mmax TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mall",mall TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mfre",mfre TSRMLS_CC);
	web3tracer_hash_add(z_entry,"mvar",mvar TSRMLS_CC);
	web3tracer_hash_add(z_entry,"calls",1 TSRMLS_CC);
}
 
 /**
 * Format output in callgrind format, with proper recursion handling
 * Format is documented at http://valgrind.org/docs/manual/cl-format.html
 * @author Constantin-Emil MARINA
 */
 static void web3tracer_process_output(TSRMLS_D){
	web3tracer_cg_cycle_t		*lastCycle=NULL,
								*newCycle,
								*cycleCursor;
	web3tracer_cg_call_t		*lastCall=NULL,
								*newCall,
								*callCursor;
	uint32						i;
	char						fname[WEB3TRACER_FNAME_BUFFER];
	const char					*fnamep;
	web3tracer_entry_chunk_t	*chunkCursor;
	web3tracer_cg_proc_t		time,
								mmax,
								mall,
								mfre,
								mvar;
	int64_t						dmem,
								dhave;

	for(chunkCursor=WEB3TRACER_G(first_entry_chunk);chunkCursor;chunkCursor=chunkCursor->next){
		for(i=0;i<chunkCursor->len;i++){
			switch(chunkCursor->entries[i].in_out){
				case 0:
					if(lastCall&&lastCall->next){
						newCall=lastCall->next;
					} else {
						newCall=malloc(sizeof(web3tracer_cg_call_t));
						if(lastCall)
							lastCall->next=newCall;
						newCall->prev=lastCall;
						newCall->next=NULL;
					}
					if(
						chunkCursor->entries[i].class_name &&
						strlen(chunkCursor->entries[i].class_name)
					){
						snprintf(fname,WEB3TRACER_FNAME_BUFFER,"%s::%s",chunkCursor->entries[i].class_name,chunkCursor->entries[i].func_name);
						fnamep=fname;
					} else {
						if(chunkCursor->entries[i].include_file){
							snprintf(fname,WEB3TRACER_FNAME_BUFFER,"%s(%s)",chunkCursor->entries[i].func_name,chunkCursor->entries[i].include_file);
							fnamep=fname;
						} else {
							fnamep=chunkCursor->entries[i].func_name;
						}
					}
					newCall->fn=strdup(fnamep);
					newCall->cycleSource=newCall;
					newCall->firstCycleSource=newCall;
					newCall->cycle=0;
					web3tracer_cg_start(time,chunkCursor->entries[i].time);
					web3tracer_cg_start(mmax,chunkCursor->entries[i].mmax);
					web3tracer_cg_start(mall,chunkCursor->entries[i].mem);
					web3tracer_cg_start(mfre,chunkCursor->entries[i].mem);
					web3tracer_cg_start(mvar,chunkCursor->entries[i].mem);
					for(callCursor=newCall->prev;callCursor;callCursor=callCursor->firstCycleSource->prev){
						if(strcmp(newCall->fn,callCursor->fn)==0){
							newCall->cycleSource=callCursor;
							if(callCursor->cycle){
								newCall->cycle=callCursor->cycle;
								newCall->firstCycleSource=callCursor->firstCycleSource;
							} else {
								newCall->firstCycleSource=callCursor;
								newCycle=malloc(sizeof(web3tracer_cg_cycle_t));
								newCycle->prev=lastCycle;
								lastCycle=newCycle;
								snprintf(fname,WEB3TRACER_FNAME_BUFFER,"<CYCLE %s>",newCall->fn);
								newCycle->name=strdup(fname);
								newCycle->time.amount=0;
								newCycle->mmax.amount=0;
								newCycle->mall.amount=0;
								newCycle->mfre.amount=0;
								newCycle->mvar.amount=0;
								newCall->cycle=callCursor->cycle=newCycle;
							}
						}
					}
					lastCall=newCall;
					break;
				case 1:
					web3tracer_cg_stop(time,chunkCursor->entries[i].time-lastCall->time.start);
					web3tracer_cg_stop(mmax,chunkCursor->entries[i].mmax-lastCall->mmax.start);
					dmem=chunkCursor->entries[i].mem-lastCall->mvar.start;
					dhave=lastCall->mall.childAmounts-lastCall->mfre.childAmounts;
					if(dmem>=dhave){
						web3tracer_cg_stop(mall,lastCall->mfre.childAmounts+dmem);
						web3tracer_cg_stop(mfre,lastCall->mfre.childAmounts);
						web3tracer_cg_stop(mvar,2*lastCall->mfre.childAmounts+dmem);
					} else {
						web3tracer_cg_stop(mall,lastCall->mall.childAmounts);
						web3tracer_cg_stop(mfre,lastCall->mall.childAmounts-dmem);
						web3tracer_cg_stop(mvar,2*lastCall->mall.childAmounts-dmem);
					}
					web3tracer_add_output_internal(lastCall->fn,(uint64)(1000*get_us_from_tsc(time.internalAmount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)]) TSRMLS_CC),mmax.internalAmount,mall.internalAmount,mfre.internalAmount,mvar.internalAmount);
					if(lastCall->cycle){
						web3tracer_add_output_call(lastCall->cycle->name,lastCall->fn,(uint64)(1000*get_us_from_tsc(time.totalAmount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])),mmax.totalAmount,mall.totalAmount,mfre.totalAmount,mvar.totalAmount TSRMLS_CC);
						if(lastCall->cycleSource==lastCall&&lastCall->prev){
							web3tracer_add_output_call(lastCall->prev->fn,lastCall->cycle->name,(uint64)(1000*get_us_from_tsc(lastCall->cycle->time.amount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])),lastCall->cycle->mmax.amount,lastCall->cycle->mall.amount,lastCall->cycle->mfre.amount,lastCall->cycle->mvar.amount TSRMLS_CC);
							lastCall->cycle->time.amount=0;
							lastCall->cycle->mmax.amount=0;
							lastCall->cycle->mall.amount=0;
							lastCall->cycle->mfre.amount=0;
							lastCall->cycle->mvar.amount=0;
						}
					} else {
						if(lastCall->prev){
							web3tracer_add_output_call(lastCall->prev->fn,lastCall->fn,(uint64)(1000*get_us_from_tsc(time.totalAmount,WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])),mmax.totalAmount,mall.totalAmount,mfre.totalAmount,mvar.totalAmount TSRMLS_CC);
						}
					}
					free(lastCall->fn);
					lastCall=lastCall->prev;
					break;
			}
		}
	}
	while(lastCycle) {
		cycleCursor=lastCycle->prev;
		free(lastCycle->name);
		free(lastCycle);
		lastCycle=cycleCursor;
	}
	while(lastCall){
		callCursor=lastCall->prev;
		free(lastCall);
		lastCall=callCursor;
	};
}
 
/**
 * Format output in XDebug trace_format 1
 * Format is documented at http://xdebug.org/docs/execution_trace
 * @author Constantin-Emil MARINA
 */
static void web3tracer_output_xt(FILE *output_file TSRMLS_DC){
	char						fname[WEB3TRACER_FNAME_BUFFER];
	const char					*fnamep;
	uint32						i;
	int							level=0,
								fs,
								cs;
	web3tracer_entry_chunk_t	*chunkCursor;
			
	fprintf(output_file,"Version: web3Tracer.1\n");
	fprintf(output_file,"Format: 2\n");
	fprintf(output_file,"\n");
	for(chunkCursor=WEB3TRACER_G(first_entry_chunk);chunkCursor;chunkCursor=chunkCursor->next){
		for(i=0;i<chunkCursor->len;i++){
			switch(chunkCursor->entries[i].in_out){
				case 0:
					if(chunkCursor->entries[i].class_name){
						snprintf(fname,WEB3TRACER_FNAME_BUFFER,"%s::%s",chunkCursor->entries[i].class_name,chunkCursor->entries[i].func_name);
						fnamep=fname;
					} else
						fnamep=chunkCursor->entries[i].func_name;
					fprintf(
						output_file,
						"%d\t%d\t0\t%0.6f\t\t%s\t%d\t%s\t%s\t%u\n",
						level,
						chunkCursor->entries[i].call_no,
						get_us_from_tsc(chunkCursor->entries[i].time-WEB3TRACER_G(init_time),WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])/1000000,
						fnamep,
						chunkCursor->entries[i].internal_user,
						chunkCursor->entries[i].include_file?chunkCursor->entries[i].include_file:"",
						chunkCursor->entries[i].call_file?chunkCursor->entries[i].call_file:"",
						chunkCursor->entries[i].call_line_no
					);
					level++;
					break;
				case 1:
					level--;
					fprintf(
						output_file,
						"%d\t%d\t1\t%0.6f\n",
						level,
						chunkCursor->entries[i].call_no,
						get_us_from_tsc(chunkCursor->entries[i].time-WEB3TRACER_G(init_time),WEB3TRACER_G(cpu_frequencies)[WEB3TRACER_G(cur_cpu_id)])/1000000
					);
					break;
			}
		}
	}
}

/**
 * Allocate (if needed) an entry and return it
 *
 * @author Constantin-Emil Marina
 */
static web3tracer_entry_t *web3tracer_alloc_call(TSRMLS_D){
	web3tracer_entry_chunk_t	*new_chunk;
	web3tracer_entry_t			*ret;
	uint32						*len=&(WEB3TRACER_G(last_entry_chunk)->len);
	
	if(*len==WEB3TRACER_CALL_LIST_INCREMENT){
		new_chunk=web3tracer_alloc_locked(sizeof(web3tracer_entry_chunk_t));
		new_chunk->len=1;
		new_chunk->next=NULL;
		WEB3TRACER_G(last_entry_chunk)->next=new_chunk;
		WEB3TRACER_G(last_entry_chunk)=new_chunk;
		WEB3TRACER_G(next_entry)=&(new_chunk->entries[1]);
		return &(new_chunk->entries[0]);
	}
	ret=WEB3TRACER_G(next_entry);
	WEB3TRACER_G(next_entry)++;
	(*len)++;
	return ret;
}

/**
 * Add function begin entry.
 *
 * @author Constantin-Emil Marina
 */
static void web3tracer_add_in(uint32 call_no, uint64 time, const char *func_name, const char *class_name, int internal_user, char *include_file, const char *call_file, uint call_line_no, int dealloc_include_file TSRMLS_DC){
	web3tracer_entry_t	*entry;
	
	entry=web3tracer_alloc_call(TSRMLS_C);
	entry->call_no					=call_no;
	entry->in_out					=0;
	entry->time						=time;
	entry->mmax						=zend_memory_peak_usage(0);
	entry->mem						=zend_memory_usage(0);
	entry->func_name				=func_name;
	entry->class_name				=class_name;
	entry->internal_user			=internal_user;
	entry->include_file				=include_file;
	entry->call_file				=call_file;
	entry->call_line_no				=call_line_no;
	entry->dealloc_include_file		=dealloc_include_file;
	entry->tagNo					=0;
	
	WEB3TRACER_G(lastInEntry)=entry;
	
}
/**
 * Add function end entry.
 *
 * @author Constantin-Emil Marina
 */
static void web3tracer_add_out(uint32 call_no, uint64 time TSRMLS_DC){
	web3tracer_entry_t	*entry;
	entry=web3tracer_alloc_call(TSRMLS_C);
	entry->call_no		=call_no;
	entry->in_out		=1;
	entry->time			=time;
	entry->mmax			=zend_memory_peak_usage(0);
	entry->mem						=zend_memory_usage(0);
}
/**
 * Replace last call in the entry list with an end entry
 *
 * @author Constantin-Emil Marina
 */
static void web3tracer_replace_out(uint32 call_no TSRMLS_DC){
	web3tracer_entry_t	*entry;
	entry=&(WEB3TRACER_G(last_entry_chunk)->entries[WEB3TRACER_G(last_entry_chunk)->len-1]);
	entry->call_no		=call_no;
	entry->in_out		=1;
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

/**
 * Add function begin entry, with formation of function name.
 * Largely copied from main/main.c#php_verror
 * @author Constantin-Emil Marina
 */
static int web3tracer_in(int internal_user TSRMLS_DC) {
	const char	*function;
	const char	*include_file=NULL;
	const char	*class_name=NULL;
	int			is_function;

	if (EG(current_execute_data) &&
		EG(current_execute_data)->opline &&
		EG(current_execute_data)->opline->opcode == ZEND_INCLUDE_OR_EVAL
	) {
#if PHP_VERSION_ID >= 50400
		switch (EG(current_execute_data)->opline->extended_value) {
#else
		switch (EG(current_execute_data)->opline->op2.u.constant.value.lval) {
#endif
			case ZEND_EVAL:
				function = "eval";
				is_function = 1;
				break;
			case ZEND_INCLUDE:
				function = "include";
				include_file=zend_get_executed_filename(TSRMLS_C);
				is_function = 1;
				break;
			case ZEND_INCLUDE_ONCE:
				function = "include_once";
				include_file=zend_get_executed_filename(TSRMLS_C);
				is_function = 1;
				break;
			case ZEND_REQUIRE:
				function = "require";
				include_file=zend_get_executed_filename(TSRMLS_C);
				is_function = 1;
				break;
			case ZEND_REQUIRE_ONCE:
				function = "require_once";
				include_file=zend_get_executed_filename(TSRMLS_C);
				is_function = 1;
				break;
			default:
				function = "ZEND_INCLUDE_OR_EVAL";
		}
	} else {
		function = get_active_function_name(TSRMLS_C);
		if (!function || !strlen(function)) {
			return 0;
		} else {
			is_function = 1;
			class_name = get_active_class_name(NULL TSRMLS_CC);
		}
	}
	web3tracer_add_in(
		++WEB3TRACER_G(call_no),
		cycle_timer(),
		function,
		class_name,
		internal_user,
		include_file,														// const char * to char * cast
		EG(current_execute_data)&&EG(current_execute_data)->op_array?
			EG(current_execute_data)->op_array->filename:NULL,
		EG(current_execute_data)&&EG(current_execute_data)->opline?
			EG(current_execute_data)->opline->lineno:
			0,
		0
		TSRMLS_CC
	);
	return 1;
}
static void web3tracer_out(uint32 call_no TSRMLS_DC){
	web3tracer_add_out(
		call_no,
		cycle_timer()
		TSRMLS_CC
	);
}
static void web3tracer_procTag1(web3tracer_entry_t *currentEntry TSRMLS_DC){
	if(currentEntry){
		if(
			currentEntry->tagNo
		){
			web3tracer_out(currentEntry->tagNo TSRMLS_CC);
			currentEntry->tagNo=0;
		}
	}
}

static void web3tracer_procTag2(TSRMLS_D){
	if(WEB3TRACER_G(lastInEntry)){
		web3tracer_entry_t	*target=WEB3TRACER_G(lastInEntry);
		if(
			(
				WEB3TRACER_G(reqEndTag) ||
				WEB3TRACER_G(reqNewTag)
			) &&
			target->tagNo
		){
			web3tracer_out(target->tagNo);
			target->tagNo=0;
			WEB3TRACER_G(reqEndTag)=0;
		}
		if(WEB3TRACER_G(reqNewTag)){
			web3tracer_add_in(
				++WEB3TRACER_G(call_no),
				cycle_timer(),
				"(tag)",
				NULL,
				1,
				WEB3TRACER_G(reqNewTag),
				NULL,
				0,
				1
				TSRMLS_CC
			);
			WEB3TRACER_G(reqNewTag)=0;
			target->tagNo=WEB3TRACER_G(call_no);
		}
		WEB3TRACER_G(lastInEntry)=target;
	}
}

static void web3tracer_procTag3(TSRMLS_D){
	if(WEB3TRACER_G(parentInEntry)){
		if(
			WEB3TRACER_G(parentInEntry)->tagNo
		){
			web3tracer_replace_out(WEB3TRACER_G(parentInEntry)->tagNo TSRMLS_CC);
			web3tracer_add_out(0,WEB3TRACER_G(lastInEntry)->time TSRMLS_CC);
			WEB3TRACER_G(parentInEntry)->tagNo=0;
		} else {
			web3tracer_replace_out(0 TSRMLS_CC);
		}
	}
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

#if PHP_VERSION_ID >= 50500
void web3tracer_execute_ex(zend_execute_data *execute_data TSRMLS_DC) {
#else
void web3tracer_execute(zend_op_array *op_array TSRMLS_DC) {
#endif
	web3tracer_entry_t		*parentEntry=WEB3TRACER_G(lastInEntry),
							*parentParentEntry=WEB3TRACER_G(parentInEntry),
							*currentEntry=0;
	int						begun=web3tracer_in(1 TSRMLS_CC);
	WEB3TRACER_G(parentInEntry)=parentEntry;
	if(begun){
		currentEntry=WEB3TRACER_G(lastInEntry);
	}
	
	uint32	now_call_no=WEB3TRACER_G(call_no);
	
#if PHP_VERSION_ID >= 50500
	WEB3TRACER_G(_zend_execute_ex)(execute_data TSRMLS_CC);
#else
	WEB3TRACER_G(_zend_execute)(op_array TSRMLS_CC);
#endif
  
	if(WEB3TRACER_G(enabled)){
		if(begun){
			web3tracer_procTag1(currentEntry TSRMLS_CC);
			web3tracer_out(now_call_no TSRMLS_CC);
		}
		WEB3TRACER_G(lastInEntry)=parentEntry;
		WEB3TRACER_G(parentInEntry)=parentParentEntry;
		web3tracer_procTag2(TSRMLS_C);
	}
}

#undef EX
#define EX(element) ((execute_data_ptr)->element)

#if PHP_VERSION_ID >= 50500
	#define EX_T(offset) (*EX_TMP_VAR(execute_data_ptr, offset))
#else
	#define EX_T(offset) (*(temp_variable *)((char *) execute_data_ptr->Ts + offset))
#endif

/**
 * Very similar to web3tracer_execute. Proxy for zend_execute_internal().
 * Applies to zend builtin functions.
 *
 * @author hzhao, kannan
 */

#if PHP_VERSION_ID >= 50500
void web3tracer_execute_internal(zend_execute_data *execute_data_ptr, zend_fcall_info *fci, int return_value_used TSRMLS_DC){
#else
void web3tracer_execute_internal (zend_execute_data *execute_data_ptr, int return_value_used TSRMLS_DC){
#endif
	web3tracer_entry_t		*parentEntry=WEB3TRACER_G(lastInEntry),
							*parentParentEntry=WEB3TRACER_G(parentInEntry),
							*currentEntry=0;
	int						begun=web3tracer_in(0 TSRMLS_CC);
	WEB3TRACER_G(parentInEntry)=parentEntry;
	if(begun){
		currentEntry=WEB3TRACER_G(lastInEntry);
	}

	uint32	now_call_no=WEB3TRACER_G(call_no);

  if (!WEB3TRACER_G(_zend_execute_internal)) {
    /* no old override to begin with. so invoke the builtin's implementation  */
    zend_op *opline = EX(opline);
#if ZEND_EXTENSION_API_NO >= 220100525
    temp_variable *retvar = &EX_T(opline->result.var);
    ((zend_internal_function *) EX(function_state).function)->handler(
                       opline->extended_value,
                       retvar->var.ptr,
                       (EX(function_state).function->common.fn_flags & ZEND_ACC_RETURN_REFERENCE) ?
                       &retvar->var.ptr:NULL,
                       EX(object), return_value_used TSRMLS_CC);
#else
    ((zend_internal_function *) EX(function_state).function)->handler(
                       opline->extended_value,
                       EX_T(opline->result.u.var).var.ptr,
                       EX(function_state).function->common.return_reference ?
                       &EX_T(opline->result.u.var).var.ptr:NULL,
                       EX(object), return_value_used TSRMLS_CC);
#endif
  } else {
    /* call the old override */
#if PHP_VERSION_ID >= 50500
	WEB3TRACER_G(_zend_execute_internal)(execute_data_ptr, fci, return_value_used TSRMLS_CC);
#else
	WEB3TRACER_G(_zend_execute_internal)(execute_data_ptr, return_value_used TSRMLS_CC);
#endif
  }
	if(WEB3TRACER_G(enabled)){
		if(begun){
			web3tracer_procTag1(currentEntry TSRMLS_CC);
			web3tracer_out(now_call_no TSRMLS_CC);
		}
		WEB3TRACER_G(lastInEntry)=parentEntry;
		WEB3TRACER_G(parentInEntry)=parentParentEntry;
		web3tracer_procTag2(TSRMLS_C);
	}
}

/**
 * Proxy for zend_compile_file(). Used to profile PHP compilation time.
 *
 * @author kannan, hzhao, Constantin-Emil Marina
 */
zend_op_array* web3tracer_compile_file(zend_file_handle *file_handle,
                                             int type TSRMLS_DC) {
	web3tracer_entry_t		*parentEntry=WEB3TRACER_G(lastInEntry);
	zend_execute_data		*data;
	uint32					now_call_no;
	zend_op_array*			ret;
	char					*filename_copy;
	
	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_add_in(
			++WEB3TRACER_G(call_no),
			cycle_timer(),
			"(compile)",
			NULL,
			0,
			NULL,
			data->op_array?data->op_array->filename:NULL,
			data->opline?data->opline->lineno:0,
			0
			TSRMLS_CC
		);
	}
	data = EG(current_execute_data);
	filename_copy=strdup(file_handle->filename);
	web3tracer_add_in(
		++WEB3TRACER_G(call_no),
		cycle_timer(),
		"(compile)",
		NULL,
		0,
		filename_copy,
		data->op_array?data->op_array->filename:NULL,
		data->opline?data->opline->lineno:0,
		1
		TSRMLS_CC
	);
	now_call_no=WEB3TRACER_G(call_no);
	ret=WEB3TRACER_G(_zend_compile_file)(file_handle, type TSRMLS_CC);
	web3tracer_out(now_call_no TSRMLS_CC);
	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_out(now_call_no-1 TSRMLS_CC);
	}
	WEB3TRACER_G(lastInEntry)=parentEntry;
	return ret;
}

/**
 * Proxy for zend_compile_string(). Used to profile PHP eval compilation time.
 */
zend_op_array* web3tracer_compile_string(zval *source_string, char *filename TSRMLS_DC) {
	web3tracer_entry_t		*parentEntry=WEB3TRACER_G(lastInEntry);
	zend_execute_data		*data;
	uint32					now_call_no;
	zend_op_array*			ret;
	char					*filename_copy;

	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_add_in(
			++WEB3TRACER_G(call_no),
			cycle_timer(),
			"(compile)",
			NULL,
			0,
			NULL,
			data->op_array?data->op_array->filename:NULL,
			data->opline?data->opline->lineno:0,
			0
			TSRMLS_CC
		);
	}
	data = EG(current_execute_data);
	filename_copy=strdup(filename);
	web3tracer_add_in(
		++WEB3TRACER_G(call_no),
		cycle_timer(),
		"(compile)",
		NULL,
		0,
		filename_copy,
		data->op_array?data->op_array->filename:NULL,
		data->opline?data->opline->lineno:0,
		1
		TSRMLS_CC
	);
	now_call_no=WEB3TRACER_G(call_no);
	ret=WEB3TRACER_G(_zend_compile_string)(source_string, filename TSRMLS_CC);
	web3tracer_out(now_call_no TSRMLS_CC);
	if(WEB3TRACER_G(opt_separateCompileFunc)){
		web3tracer_out(now_call_no-1 TSRMLS_CC);
	}
	WEB3TRACER_G(lastInEntry)=parentEntry;
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
	/* Remove proxies, restore the originals */
#if PHP_VERSION_ID >= 50500
	zend_execute_ex     = WEB3TRACER_G(_zend_execute_ex);
#else
    zend_execute        = WEB3TRACER_G(_zend_execute);
#endif
	zend_execute_internal = WEB3TRACER_G(_zend_execute_internal);
	zend_compile_file     = WEB3TRACER_G(_zend_compile_file);
	zend_compile_string = WEB3TRACER_G(_zend_compile_string);

	/* Resore cpu affinity. */
	restore_cpu_affinity(&WEB3TRACER_G(prev_mask));
}

/**
 * Free data structures
 *
 * @author Constantin-Emil Marina
 */
static void web3tracer_free(TSRMLS_D){
	web3tracer_entry_chunk_t	*chunk_cursor;
	web3tracer_entry_t			*entry;
	int							i;
	
	while(WEB3TRACER_G(first_entry_chunk)) {
		for(i=0;i<WEB3TRACER_G(first_entry_chunk)->len;i++)
			if(!WEB3TRACER_G(first_entry_chunk)->entries[i].in_out&&WEB3TRACER_G(first_entry_chunk)->entries[i].dealloc_include_file){
				free(WEB3TRACER_G(first_entry_chunk)->entries[i].include_file);
			}
		chunk_cursor=WEB3TRACER_G(first_entry_chunk)->next;
		web3tracer_free_locked(WEB3TRACER_G(first_entry_chunk),sizeof(web3tracer_entry_chunk_t));
		WEB3TRACER_G(first_entry_chunk)=chunk_cursor;
	}
}
