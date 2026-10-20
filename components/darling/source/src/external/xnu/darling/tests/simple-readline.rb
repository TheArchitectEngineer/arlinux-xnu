require 'tempfile'
source_path = ARGV[0] || File.expand_path('../src/libsystem_kernel/emulation/src/common/simple.c', __dir__)
source = File.read(source_path)
body = source[/char\* __simple_readline\(.*?\n\}/m] or abort 'function not found'
test = <<~C
  #include <assert.h>
  #include <string.h>
  #include <stddef.h>
  struct simple_readline_buf { char buf[512]; int used; };
  #define min(a,b) ((a)<(b)?(a):(b))
  static int calls, result, next_result;
  static const char *payload;
  static int sys_read(int fd, char *buf, size_t size) {
    assert(++calls <= 2);
    int returned = result;
    if (returned > 0) {
      assert((size_t)returned <= size);
      memcpy(buf, payload, returned);
    }
    result = next_result;
    return returned;
  }
  #{body}
  int main(void) {
    struct simple_readline_buf b = {0}; char out[512];
    result=-9; calls=0;
    assert(__simple_readline(-1,&b,out,sizeof out)==NULL);
    assert(b.used==0);
    memcpy(b.buf,"partial",7); b.used=7; calls=0; result=-9;
    assert(__simple_readline(-1,&b,out,sizeof out)==out);
    assert(strcmp(out,"partial")==0 && b.used==0);
    result=0; calls=0;
    assert(__simple_readline(-1,&b,out,sizeof out)==NULL);
    memcpy(b.buf,"hello\\n",6); b.used=6; calls=0;
    assert(__simple_readline(-1,&b,out,sizeof out)==out);
    assert(strcmp(out,"hello")==0 && calls==0 && b.used==0);
    payload="fresh\\nrest"; result=10; next_result=-9; calls=0;
    assert(__simple_readline(-1,&b,out,sizeof out)==out);
    assert(strcmp(out,"fresh")==0 && calls==1 && b.used==4);
    calls=0;
    assert(__simple_readline(-1,&b,out,sizeof out)==out);
    assert(strcmp(out,"rest")==0 && calls==1 && b.used==0);
    payload="partial"; result=7; next_result=0; calls=0;
    assert(__simple_readline(-1,&b,out,sizeof out)==out);
    assert(strcmp(out,"partial")==0 && calls==2 && b.used==0);
  }
C
Tempfile.create(['readline', '.c']) do |f|
  f.write(test); f.flush
  exe = f.path + '.out'
  begin
    abort 'compile failed' unless system('clang','-fsanitize=address,undefined',f.path,'-o',exe)
    abort 'test failed' unless system(exe)
    puts 'PASS: errors, EOF, buffered/newly read newline, retained suffix and partial read'
  ensure
    File.delete(exe) if File.exist?(exe)
  end
end
