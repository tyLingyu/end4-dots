// LeakSanitizer reads this at startup (ASan builds only). Linked into each executable directly:
// a static-library object that nothing references would never be pulled in.
//
// fontconfig and Pango keep process-lifetime font caches (pattern sets, matched fonts, the font
// map) that are never freed by design; LSan reports them at exit. ii-shell never allocates
// through these libraries itself, so suppressing by library is safe.
//
// The PipeWire entries are Noctalia's: PipeWire unloads SPA modules through atexit, after LSan
// has checked for leaks.
extern "C" const char* __lsan_default_suppressions() {
  return "leak:libfontconfig.so\n"
         "leak:libpangoft2-1.0.so\n"
         "leak:libpango-1.0.so\n"
         "leak:pw_context_load_module\n"
         "leak:pw_context_new\n";
}
