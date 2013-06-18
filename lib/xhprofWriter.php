<?php
	/**
	 * This file provides the xhprof output helper functions to output
	 * files for the xhprof display libraries.
	 *
	 * @package web3tracer
	 * @copyright EXTEON www.exteon.ro
	 * @author Constantin-Emil MARINA
	 * @license Apache License, Version 2.0
	 * @version 2.0.1
	 */

	/**
	 * Converts data returned from web3tracer_disable to the same format
	 * returned by XHProf's xhprof_disable. Its output can be used with 
	 * xhprof's own writing functions or with web3tracer_xhprofWrite below
	 *  
	 * @param mixed $data
	 * Data returned by web3tracer_disable()
	 * 
	 * @return mixed
	 * XHProf formatted data
	 */
	function web3tracer_xhprofConvert($data){
		$result=array();
		foreach($data as $fName=>$fData){
			$main=false;
			if($fName=='{main}'){
				$fName='main()';
				$main=true;
				$wt=$fData['stats']['time']/1000;
				$mu=$fData['stats']['mall']-$fData['stats']['mfre'];
				$pmu=$fData['stats']['mmax'];
			}
			$fName=str_replace(array('<','>'),array('[',']'),$fName);
			if(isset($fData['callees'])){
				foreach($fData['callees'] as $cName=>$cData){
					$cName=str_replace(array('<','>'),array('[',']'),$cName);
					$result["$fName==>$cName"]=array(
							'ct'=>$cData['stats']['calls'],
							'wt'=>$cData['stats']['time']/1000,
							'mu'=>$cData['stats']['mall']-$cData['stats']['mfre'],
							'pmu'=>$cData['stats']['mmax']
					);
					if($main){
						$wt+=$cData['stats']['time']/1000;
						$mu+=$cData['stats']['mall']->$cData['stats']['mfre'];
						$pmu+=$cData['stats']['mmax'];
					}
				}
			}
			if($main){
				$result[$fName]=array(
					'ct'=>$fData['stats']['calls'],
					'wt'=>$wt,
					'mu'=>$mu,
					'pmu'=>$pmu
				);
			}
		}
		return $result;
	}
	
	/**
	 * Function to write profiler file in xhprof format.
	 * 
	 * @param mixed $data
	 * The data returned by web3tracer_disabled
	 * 
	 * @param string $filename
	 * The output file name. If no filename is specified, a file name of the
	 * form 2013-05-24-12-59-03.xhprof.xhprof will be generated
	 * 
	 * @param string $path
	 * The path of the destination directory; if none is specified, the current
	 * directory is assumed.
	 * 
	 * @return boolean|string
	 * Returns false in case of failure (destination file is not writable).
	 * Otherwise it returns the path and name of the saved file.
	 */
	function web3tracer_xhprofWrite($data,$filename=null,$path=null){
		if(!$path){
			$path='';
		}
		if(!$filename){
			$filename=date('Y-m-d-H-i-s').'.xhprof.xhprof';
		}
		$outputFile=$path;
		if(
				$outputFile&&
				$outputFile[strlen($outputFile)-1]!='/'
		){
			$outputFile.='/';
		}
		$outputFile.=$filename;
		$f=fopen($outputFile,'w');
		if(!$f){
			return false;
		}
		fwrite($f,serialize(web3tracer_xhprofConvert($data)));
		fclose($f);
		return $outputFile;
	}
