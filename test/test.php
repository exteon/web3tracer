<?php
	function wait(){
		usleep(10);
	}
	function a(){
		web3tracer_tag('waitSome');
		wait();
		web3tracer_tag('goToB');
		$GLOBALS['iterations']--;
		if($GLOBALS['iterations']>0){
			include('test-b.php');
		}
		$GLOBALS['iterations']--;
		if($GLOBALS['iterations']>0){
			include('test-b.php');
		}
		web3tracer_endTag();
		wait();
	}
	$iterations=10000;

	web3tracer_enable();
	
	a();
	
	$data=web3tracer_disable(WEB3TRACER_OUTPUT_PROCESSED);
	var_dump($data);
	include_once('../lib/callgrindWriter.php');
	include_once('../lib/xhprofWriter.php');
	web3tracer_callgrindWrite($data);
	web3tracer_xhprofWrite($data);

	echo "Done.\n";
