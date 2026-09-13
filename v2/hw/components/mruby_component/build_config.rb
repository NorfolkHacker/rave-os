# The esp32p4 CrossBuild below is built with CC/LD/AR pointed at the ESP-IDF
# cross toolchain via the environment (see CMakeLists.txt). mruby's own build
# process needs an `mrbc` (the bytecode compiler mruby's mrbgems are compiled
# with) that runs on THIS machine, not on the target -- so a `host` build is
# declared here, before the cross build.
#
# Declaring it isn't enough by itself: `MRuby::Build#create_mrbc_build` (run
# automatically for any build named "host", see lib/mruby/build.rb around the
# `current.host?` check) deep-clones this build's own compiler/linker/archiver
# commands into a generated "host/mrbc" build that actually compiles mrbc.
# `toolchain :gcc` resolves CC/LD/AR from the environment (tasks/toolchains/
# gcc.rake), so left alone it would pick up the same cross toolchain the
# esp32p4 build uses, and the clone would inherit it -- producing an mrbc
# binary that can't run on the build machine. The four command overrides below
# pin this build to the real host toolchain regardless of what CC/LD/AR are
# set to for the cross build.
MRuby::Build.new('host') do |conf|
  toolchain :gcc
  conf.cc.command = 'gcc'
  conf.cxx.command = 'g++'
  conf.linker.command = 'gcc'
  conf.archiver.command = 'ar'
end

MRuby::CrossBuild.new('esp32p4') do |conf|
  toolchain :gcc

  conf.cc do |cc|
    cc.include_paths << (ENV["COMPONENT_INCLUDES"] || "").split(';')
    cc.flags << '-std=gnu17'
    cc.flags = cc.flags.flatten.collect { |x| x.gsub('-MP', '') }
    cc.defines << %w(ESP_PLATFORM)
  end

  conf.cxx do |cxx|
    cxx.include_paths = conf.cc.include_paths.dup
    cxx.flags = cxx.flags.flatten.collect { |x| x.gsub('-MP', '') }
    cxx.defines = conf.cc.defines.dup
  end

  conf.bins = []
  conf.build_mrbtest_lib_only
  conf.disable_cxx_exception

  conf.gem :core => "mruby-print"
  conf.gem :core => "mruby-compiler"
end
