
PHP_ARG_ENABLE(web3tracer, whether to enable web3tracer support,
[ --enable-web3tracer      Enable web3tracer support])

if test "$PHP_WEB3TRACER" != "no"; then
  AC_FUNC_MMAP()
  PHP_NEW_EXTENSION(web3tracer, web3tracer.c, $ext_shared)
fi
