#!/usr/bin/env python3
"""Build CoreAudio and its device-enumeration regression check."""
from pathlib import Path
import shlex
import subprocess

project=Path(__file__).resolve().parent.parent
build=project/'build/darling-src/build'
framework=build/'src/CoreAudio/CoreAudio'
cpp_flags={}
for line in (framework/'CMakeFiles/CoreAudio.dir/flags.make').read_text().splitlines():
    if line.startswith('CXX_') and ' = ' in line:
        key,value=line.split(' = ',1);cpp_flags[key]=shlex.split(value)
link=shlex.split((framework/'CMakeFiles/CoreAudio.dir/link.txt').read_text())
for name in ['AudioHardware','AudioHardwareImpl','pulse/AudioHardwareImplPA',
             'pulse/PADispatchMainLoop','pulse/AudioHardwareStreamPA',
             'pulse/AudioHardwareStreamPAOutput','pulse/AudioHardwareStreamPAInput',
             'pulse/AudioHardwareImplPAOutput','pulse/AudioHardwareImplPAInput']:
    obj=project/f'build/riot-audio-{Path(name).name}.o'
    subprocess.run(['clang++',*cpp_flags['CXX_DEFINES'],*cpp_flags['CXX_INCLUDES'],
        *cpp_flags['CXX_FLAGS'],'-O2','-c',str(project/f'build/darling-src/src/CoreAudio/CoreAudio/{name}.cpp'),
        '-o',str(obj)],check=True)
    link[link.index(f'CMakeFiles/CoreAudio.dir/{name}.cpp.o')]=str(obj)
link[link.index('-o')+1]=str(project/'build/riot-CoreAudio')
subprocess.run(link,cwd=framework,check=True)
component=build/'src/CoreAudio/CoreAudioComponent'
component_flags={}
for line in (component/'CMakeFiles/CoreAudioComponent.dir/flags.make').read_text().splitlines():
    if line.startswith('CXX_') and ' = ' in line:
        key,value=line.split(' = ',1);component_flags[key]=shlex.split(value)
obj=project/'build/riot-AUHAL.o'
subprocess.run(['clang++',*component_flags['CXX_DEFINES'],*component_flags['CXX_INCLUDES'],
    *component_flags['CXX_FLAGS'],'-O2','-c',str(project/'build/darling-src/src/CoreAudio/CoreAudioComponent/AUHAL.cpp'),
    '-o',str(obj)],check=True)
link=shlex.split((component/'CMakeFiles/CoreAudioComponent.dir/link.txt').read_text())
link[link.index('CMakeFiles/CoreAudioComponent.dir/AUHAL.cpp.o')]=str(obj)
obj=project/'build/riot-MatrixMixerAU.o'
subprocess.run(['clang++',*component_flags['CXX_DEFINES'],*component_flags['CXX_INCLUDES'],
    *component_flags['CXX_FLAGS'],'-O2','-c',str(project/'build/darling-src/src/CoreAudio/CoreAudioComponent/MatrixMixerAU.cpp'),
    '-o',str(obj)],check=True)
matrix_obj='CMakeFiles/CoreAudioComponent.dir/MatrixMixerAU.cpp.o'
if matrix_obj in link:
    link[link.index(matrix_obj)]=str(obj)
else:
    link.append(str(obj))
# The AUHAL subclasses must be recompiled too: they share AUHAL.h's layout and vtable, and the default-output
# unit's SetProperty refuses kAudioOutputUnitProperty_CurrentDevice unless patched.
for name in ['DefaultOutputAU','SystemOutputAU']:
    obj=project/f'build/riot-{name}.o'
    subprocess.run(['clang++',*component_flags['CXX_DEFINES'],*component_flags['CXX_INCLUDES'],
        *component_flags['CXX_FLAGS'],'-O2','-c',str(project/f'build/darling-src/src/CoreAudio/CoreAudioComponent/{name}.cpp'),
        '-o',str(obj)],check=True)
    default_obj=f'CMakeFiles/CoreAudioComponent.dir/{name}.cpp.o'
    assert default_obj in link, f'{name} object missing from the link line'
    link[link.index(default_obj)]=str(obj)
link[link.index('-o')+1]=str(project/'build/riot-CoreAudioComponent')
subprocess.run(link,cwd=component,check=True)
toolbox=build/'src/CoreAudio/AudioToolbox'
toolbox_flags={}
for line in (toolbox/'CMakeFiles/AudioToolbox.dir/flags.make').read_text().splitlines():
    if line.startswith('CXX_') and ' = ' in line:
        key,value=line.split(' = ',1);toolbox_flags[key]=shlex.split(value)
link=shlex.split((toolbox/'CMakeFiles/AudioToolbox.dir/link.txt').read_text())
for name in ['AudioComponentManager.mm','AudioConverter.cpp','AudioConverterImpl.cpp','AUGraph.cpp','AUComponent.cpp','AudioFileComponent.cpp']:
    obj=project/f'build/riot-toolbox-{Path(name).stem}.o'
    subprocess.run(['clang++',*toolbox_flags['CXX_DEFINES'],*toolbox_flags['CXX_INCLUDES'],
        *toolbox_flags['CXX_FLAGS'],'-O2','-c',str(project/f'build/darling-src/src/CoreAudio/AudioToolbox/{name}'),
        '-o',str(obj)],check=True)
    link[link.index(f'CMakeFiles/AudioToolbox.dir/{name}.o')]=str(obj)
link[link.index('-o')+1]=str(project/'build/riot-AudioToolbox')
subprocess.run(link,cwd=toolbox,check=True)
flags={}
for line in (build/'src/frameworks/CoreVideo/CMakeFiles/CoreVideo.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        key,value=line.split(' = ',1);flags[key]=shlex.split(value)
obj=project/'build/riot-audio-device-check.o'
subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],
    '-O2','-c',str(project/'tools/riot-audio-device-check.c'),'-o',str(obj)],check=True)
subprocess.run(['clang','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib',
    '-isysroot','@DARLING_SDK@',str(obj),'-lSystem','-framework','CoreAudio','-framework','CoreFoundation',
    '-o',str(project/'build/riot-audio-device-check')],check=True)
obj=project/'build/riot-audio-unit-check.o'
subprocess.run(['clang++',*cpp_flags['CXX_DEFINES'],*cpp_flags['CXX_INCLUDES'],
    *cpp_flags['CXX_FLAGS'],'-O2','-c',str(project/'tools/riot-audio-unit-check.cpp'),'-o',str(obj)],check=True)
subprocess.run(['clang++','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib',
    '-isysroot','@DARLING_SDK@',str(obj),'-lSystem','-lc++','-framework','CoreAudio',
    '-framework','AudioToolbox','-o',str(project/'build/riot-audio-unit-check')],check=True)

obj=project/'build/riot-audio-graph-check.o'
subprocess.run(['clang++',*cpp_flags['CXX_DEFINES'],*cpp_flags['CXX_INCLUDES'],
    *cpp_flags['CXX_FLAGS'],'-O2','-c',str(project/'tools/riot-audio-graph-check.cpp'),'-o',str(obj)],check=True)
subprocess.run(['clang++','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib',
    '-isysroot','@DARLING_SDK@',str(obj),'-lSystem','-lc++',
    str(project/'build/riot-AudioToolbox'),'-o',str(project/'build/riot-audio-graph-check')],check=True)

obj=project/'build/riot-audio-input-check.o'
subprocess.run(['clang++',*cpp_flags['CXX_DEFINES'],*cpp_flags['CXX_INCLUDES'],
    *cpp_flags['CXX_FLAGS'],'-O2','-c',str(project/'tools/riot-audio-input-check.cpp'),'-o',str(obj)],check=True)
subprocess.run(['clang++','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib',
    '-isysroot','@DARLING_SDK@',str(obj),'-lSystem','-lc++','-lobjc','-framework','CoreAudio',
    '-framework','AudioToolbox','-framework','AVFoundation','-o',str(project/'build/riot-audio-input-check')],check=True)
