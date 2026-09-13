MRuby::CrossBuild.new('esp32p4') do |conf|
  toolchain :gcc

  conf.cc do |cc|
    cc.include_paths << ENV["COMPONENT_INCLUDES"].split(';')
    cc.flags << '-mlongcalls' if ENV["MRUBY_TARGET_ARCH"] == 'xtensa'
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
