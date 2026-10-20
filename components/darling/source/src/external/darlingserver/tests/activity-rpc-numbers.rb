# Compare generated wire numbers with a pre-change git revision.
# Example: ruby tests/activity-rpc-numbers.rb 6738d0f
require 'tmpdir'
require 'open3'
root = File.expand_path('..', __dir__)
baseline, status = Open3.capture2('git', '-C', root, 'show', "#{ARGV.fetch(0)}:scripts/generate-rpc-wrappers.py")
abort 'cannot read baseline generator' unless status.success?
Dir.mktmpdir('activity-rpc') do |dir|
  old_script = File.join(dir, 'baseline.py')
  File.write(old_script, baseline)
  maps = [old_script, File.join(root, 'scripts/generate-rpc-wrappers.py')].each_with_index.map do |script, i|
    header = File.join(dir, "#{i}.h")
    abort 'generation failed' unless system('python3', script, header,
      File.join(dir, "#{i}.internal.h"), File.join(dir, "#{i}.c"), '"rpc.h"')
    File.read(header).scan(/^\s*(dserver_callnum_\w+) = (.*),$/).to_h
  end
  before, after = maps
  abort 'existing call number changed' unless before.all? { |name, value| after[name] == value }
  abort 'unexpected new calls' unless after.keys - before.keys == ['dserver_callnum_mach_generate_activity_id']
  puts "PASS: #{before.length} existing wire numbers preserved; only activity-ID RPC appended"
end
