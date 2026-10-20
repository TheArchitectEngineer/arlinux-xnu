#!/usr/bin/env ruby
# Execute actual pre-init cleanup only against freshly created test directories.
require 'tmpdir'
require 'fileutils'
require 'open3'
root=File.expand_path('..',__dir__)
source=if ARGV[0]
  out,status=Open3.capture2('git','-C',root,'show',"#{ARGV[0]}:src/darlingserver.cpp")
  abort 'baseline unavailable' unless status.success?
  out
else
  File.read(File.join(root,'src/darlingserver.cpp'))
end
wipe=source[/static void wipeDir\(.*?^\}/m] or abort 'wipeDir missing'
preinit=source[/void darlingPreInit\(.*?^\}/m] or abort 'darlingPreInit missing'
code=<<~'CPP'
  #include <cstdio>
  #include <cstdlib>
  #include <cstring>
  #include <cassert>
  #include <dirent.h>
  #include <unistd.h>
  static int helpers;
  static void ensureProcSymlink(const char*) { helpers++; }
  static void ensureShSymlink(const char*) { helpers++; }
  static void ensureHomebrewSymlinks(const char*) { helpers++; }
CPP
code+=wipe+"\n"+preinit+"\n"
code+=<<~'CPP'
  int main(int argc,char **argv) {
    assert(argc==2 && strlen(argv[1])>1 && strcmp(argv[1],"/"));
    assert(!setenv("DARLING_NONROOT","1",1));
    darlingPreInit(argv[1]);
    assert(helpers==3);
    return 0;
  }
CPP
Dir.mktmpdir('preserve-var-tmp-') do |dir|
  file=File.join(dir,'probe.cpp'); File.write(file,code)
  %w[-O0 -O2].each do |optimization|
    binary=File.join(dir,'probe')
    abort 'compile failed' unless system(ENV.fetch('CXX','clang++'),'-std=c++17',optimization,
      '-g','-fsanitize=address,undefined',file,'-o',binary)
    %w[direct symlink].each do |layout|
      prefix=File.join(dir,"prefix-#{optimization}-#{layout}")
      var=File.join(prefix,layout=='direct' ? 'var' : 'private/var')
      FileUtils.mkdir_p([File.join(var,'tmp/nested'),File.join(var,'run/nested')])
      File.symlink('private/var',File.join(prefix,'var')) if layout=='symlink'
      payload="persistent\x00payload\xff".b
      saved=File.join(var,'tmp/nested/application.data'); File.binwrite(saved,payload)
      outside=File.join(dir,"outside-#{optimization}-#{layout}")
      FileUtils.mkdir_p(outside); File.write(File.join(outside,'sentinel'),'unchanged')
      3.times do
        FileUtils.mkdir_p(File.join(var,'run/nested'))
        File.write(File.join(var,'run/nested/stale.pid'),'123')
        File.symlink(outside,File.join(var,'run/external-link'))
        abort 'pre-init failed' unless system(binary,prefix)
        abort '/var/tmp payload was deleted or changed' unless File.file?(saved) && File.binread(saved)==payload
        abort '/var/run not emptied' unless Dir.children(File.join(var,'run')).empty?
        abort 'cleanup followed runtime symlink' unless File.read(File.join(outside,'sentinel'))=='unchanged'
      end
    end
  end
end
puts 'PASS: persistent var/tmp and runtime cleanup across repeated starts'
