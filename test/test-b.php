<?php
	eval('web3tracer_tag(\'tagInEval\');usleep(10);');

	$GLOBALS['iterations']--;
	if($GLOBALS['iterations']>0){
		if($GLOBALS['iterations']&1){
			web3tracer_tag('callA');
			a();
			web3tracer_endTag();
		} else {
			include('test-b.php');
		}
	}