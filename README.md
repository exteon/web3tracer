# web3tracer PHP profiler
## PHP extension to profile PHP scripts

web3tracer is a PHP profiler / tracer derived from XHProf; it is a PHP profiler module written in C allowing you to profile php script performance with minimum overhead.
 
It allows you to profile PHP scripts to analyze PHP scripts' performance, from finding bottlenecks to fine tuning execution times. It is an invaluable tool in assessing your PHP code's performance. Using the KCacheGrind graph rendition, you can visualize the execution flow of your scripts. This also gives you a structural overview of code flow, allowing you to easily identify the functional structure of your code.

---
Check out the *project homepage* at: http://www.exteon.ro/en/products/php-tools/web3tracer

And the *full documentation* at: http://www.exteon.ro/en/products/php-tools/web3tracer
---
 
This PHP profiler is a new alternative, outperforming similar tools in:
* Minimal overhead
* KCachegrind output
* XHProf output
* Correctly handles recursive calls, not by expansion (XHProf) but by decoupling
* Full call tree traces in XDebug XT format

It is not difficult now to get an execution graph like the one below:

![Kcachegrind generated graph](http://www.exteon.ro/Upload/image/web3tracer/recursion_example.png)
 
This means that when you profile php script it will be will be more accurate, and easier to follow visually. Please browse the manual for a better look at what it does.