<?php
    /*
     * Let's define this constant that tells us we're
     * running on a development server. If this is not 
     * present we'll asume we're on production
     */
    define('APP_DEVEL_SERVER',1);
    
    /*
     * If we're on devel, we use the profile _GET parameter
     * to initialize the profiling. If we're in production
     * mode, we profile one out of 1000 requests and log it
     * to /var/profiles for later analysis. For devel, one
     * should make sure PHP folder is writable, for 
     * production, that /var/profiles exists and is
     * writable.
     */
    $start_profile=false;
    $profile_output_dir='';
    if(
        defined('APP_DEVEL_SERVER')&&
        constant('APP_DEVEL_SERVER')
    ){
        if(isset($_GET['profile'])) {
            $start_profile=true;
        }
    } else {
        $start_profile=!rand(0,999);
        $profile_output_dir='/var/profiles/';
    }
    
    if($start_profile){
        web3tracer_enable();
    }
    

    {
    
        YOUR_CODE_HERE();
    
    }
    
    /*
     * Here we stop the profile and instruct web3tracer to
     * deposit the result in callgrind format, in a file
     * of the form callgrind.index_php_2012-09-16_21-48-22
     */
    if($start_profile){
        $data=web3tracer_disable(WEB3TRACER_OUTPUT_PROCESSED);
        include_once('../lib/callgrindWriter.php');
        web3tracer_callgrindWrite($data,null,$profile_output_dir);
	}
?>