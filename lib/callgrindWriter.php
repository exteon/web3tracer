<?php
	/**
	 * This file provides the callgrind output helper functions to output
	 * files for KcacheGrind.
	 *
	 * @package web3tracer
	 * @copyright EXTEON www.exteon.ro
	 * @author Constantin-Emil MARINA
	 * @license Apache License, Version 2.0
	 * @version 2.0.1
	 */

	/**
	 * Function to write profiler file in callgrind format.
	 * 
	 * @param mixed $data
	 * The data returned by web3tracer_disabled
	 * 
	 * @param string $filename
	 * The output file name. If no filename is specified, a file name of the
	 * form callgrind.2013-05-24-12-59-03 will be generated
	 * 
	 * @param string $path
	 * The path of the destination directory; if none is specified, the current
	 * directory is assumed.
	 * 
	 * @return boolean|string
	 * Returns false in case of failure (destination file is not writable).
	 * Otherwise it returns the path and name of the saved file.
	 */
	function web3tracer_callgrindWrite($data,$filename=null,$path=null){
		if(!$path){
			$path='';
		}
		if(!$filename){
			$filename='callgrind.'.date('Y-m-d-H-i-s');
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
		fwrite($f,"creator: web3tracer\n");
		fwrite($f,"cmd: $_SERVER[SCRIPT_FILENAME]\n");
		fwrite($f,"desc: Generated on ".date('Y-m-d H:i:s')."\n");
		fwrite($f,"event: time : Execution time\n");
		fwrite($f,"event: mmax : Maximum allocated memory\n");
		fwrite($f,"event: mall : Allocated memory\n");
		fwrite($f,"event: mfre : Freed memory\n");
		fwrite($f,"event: mvar : Memory operations\n");
		fwrite($f,"events: time mmax mall mfre mvar\n");
		foreach($data as $fName=>$fData){
			fwrite($f,"fl=\n");
			fwrite($f,"fn=$fName\n");
			fwrite($f,"0 {$fData['stats']['time']} {$fData['stats']['mmax']} {$fData['stats']['mall']} {$fData['stats']['mfre']} {$fData['stats']['mvar']}\n");
			if(isset($fData['callees'])){
				foreach($fData['callees'] as $cName=>$cData){
					fwrite($f,"cfl=\n");
					fwrite($f,"cfn=$cName\n");
					fwrite($f,"calls={$cData['stats']['calls']} 0\n");
					fwrite($f,"0 {$cData['stats']['time']} {$cData['stats']['mmax']} {$cData['stats']['mall']} {$cData['stats']['mfre']} {$cData['stats']['mvar']}\n");
				}
			}
		}
		fclose($f);
		return $outputFile;
	}
	