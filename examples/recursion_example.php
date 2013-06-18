<?php
    web3tracer_enable();
    
    {
    a();
    }
    
    $data=web3tracer_disable(WEB3TRACER_OUTPUT_PROCESSED);
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