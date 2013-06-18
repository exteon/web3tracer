<?php
    xhprof_enable();
    
    {
    a();
    }
    
    $data=xhprof_disable(WEB3TRACER_OUTPUT_PROCESSED);
	
	var_dump($data);
	
	$ownTimes=array();
	
	$f=fopen('xh','w');

		fwrite($f,"creator: web3tracer\n");
		fwrite($f,"cmd: $_SERVER[SCRIPT_FILENAME]\n");
		fwrite($f,"desc: Generated on ".date('Y-m-d H:i:s')."\n");
		fwrite($f,"event: time : Execution time\n");
		fwrite($f,"events: time\n");


	foreach($data as $call=>$stats){
		if(preg_match('`(.*)==>(.*)`',$call,$match)){
			$ownTimes[$match[2]]+=$stats['wt'];
			$ownTimes[$match[1]]-=$stats['wt'];
		} else {
			$ownTimes[$call]=$sats['wt'];
		}
	}
	
	foreach($data as $call=>$stats){
		if(preg_match('`(.*)==>(.*)`',$call,$match)){
			fwrite($f,"fl=\n");
			fwrite($f,"fn=$match[1]\n");
			fwrite($f,"0 0\n");
			fwrite($f,"cfl=\n");
			fwrite($f,"cfn=$match[2]\n");
			fwrite($f,"calls=$stats[ct] 0\n");
			fwrite($f,"0 $stats[wt]\n");
		}
	}
	
	foreach($ownTimes as $fn=>$time){
		fwrite($f,"fl=\n");
		fwrite($f,"fn=$fn\n");
		fwrite($f,($time)." 0\n");
	}
	
	die();
	
	include_once('../lib/callgrindWriter.php');
    include_once('../lib/xhprofWriter.php');
	web3tracer_callgrindWrite($data);
    web3tracer_xhprofWrite($data);
       
    function a(){
        usleep(1000);
        if(++$GLOBALS['cycle1']<5){
            $GLOBALS['cycle2']=0;
            b();
            c();
        }
    }
    function b(){
        usleep(2000);
        if(++$GLOBALS['cycle2']<3){
            b();
        }
        $GLOBALS['mempad'].=str_repeat('x',1024*1024);
    }
    function c(){
        usleep(3000);
        $GLOBALS['mempad']='';
        a();
    }
?>