#!/usr/bin/env python3
"""Compare C state decisions against functions compiled from unmodified upstream.
C++ is used only to execute the original reference implementation, never by the port.
"""
import ctypes as c
import os
import pathlib
import re
import shlex
import subprocess
root=pathlib.Path(__file__).resolve().parent.parent
build=root/'build';build.mkdir(exist_ok=True)
def function(source,name):
 start=source.index(name)
 start=source.rfind('\n',0,start)+1
 previous=source.rfind('\n',0,start-1)+1
 if source[previous:start].strip() in {'void','bool'}:start=previous
 brace=source.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
def enums(header):
 return '\n'.join(re.findall(r'typedef enum\s*\{.*?\}\s*\w+;',header,re.S))
hide=(root/'upstream/launcher/LauncherHideMachine.cpp').read_text()
hh=(root/'upstream/launcher/LauncherHideMachine.h').read_text()
hover=(root/'upstream/launcher/LauncherHoverMachine.cpp').read_text()
hv=(root/'upstream/launcher/LauncherHoverMachine.h').read_text()
reference='''#include <cmath>
class LauncherHideMachine { public:
'''+enums(hh)+'''
HideMode _mode=HIDE_NEVER; HideQuirk _quirks=DEFAULT; bool _should_hide=false;
void SetShouldHide(bool value,bool) { _should_hide=value; }
void EnsureHideState(bool skip_delay);
bool GetQuirk(HideQuirk,bool allow_partial=true) const;
};
'''+hide[hide.index('#define VISIBLE_REQUIRED'):hide.index('void LauncherHideMachine::EnsureHideState')]+function(hide,'LauncherHideMachine::EnsureHideState')+'\n'+function(hide,'LauncherHideMachine::GetQuirk')+'''
extern "C" int reference_hide(unsigned flags) {
 LauncherHideMachine m; m._mode=LauncherHideMachine::AUTOHIDE;
 m._quirks=(LauncherHideMachine::HideQuirk)flags; m.EnsureHideState(true);return m._should_hide;
}
class LauncherHoverMachine { public:
'''+enums(hv)+'''
HoverQuirk _quirks=DEFAULT;bool result=false;
void SetShouldHover(bool v){result=v;}
void EnsureHoverState();bool GetQuirk(HoverQuirk,bool allow_partial=true);
};
'''+function(hover,'LauncherHoverMachine::EnsureHoverState')+'\n'+function(hover,'LauncherHoverMachine::GetQuirk')+'''
extern "C" int reference_hover(unsigned flags) {
 LauncherHoverMachine m; m._quirks=(LauncherHoverMachine::HoverQuirk)flags;
 m.EnsureHoverState();return m.result;
}
'''
(build/'reference.cpp').write_text(reference)
subprocess.run([os.environ.get('CXX','c++'),'-shared','-fPIC','-O2',str(build/'reference.cpp'),'-o',str(build/'reference.so')],check=True)
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','gdk-pixbuf-2.0'],text=True))
subprocess.run([os.environ.get('CC','cc'),'-shared','-fPIC','-O2',str(root/'src/launcher-core.c'),'-o',str(build/'core.so'),*flags,'-lm'],check=True)
ref=c.CDLL(str(build/'reference.so'));port=c.CDLL(str(build/'core.so'))
class Hide(c.Structure):
 _fields_=[('mode',c.c_int),('quirks',c.c_uint),('should_hide',c.c_int),('pressure',c.c_int),
           ('reveal_pressure',c.c_int),('decay_rate',c.c_int),('reveal_progress',c.c_double),
           ('hide_deadline',c.c_int64),('decay_time',c.c_int64)]
port.hide_machine_init.argtypes=[c.POINTER(Hide)]
port.hide_machine_mode.argtypes=[c.POINTER(Hide),c.c_int,c.c_int64]
port.launcher_should_hover.argtypes=[c.c_int]*6
m=Hide()
for flags in range(1<<18):
 port.hide_machine_init(c.byref(m));m.quirks=flags
 port.hide_machine_mode(c.byref(m),1,0)
 expected=ref.reference_hide(flags)
 assert bool(m.should_hide)==bool(expected),(hex(flags),m.should_hide,expected)
for flags in range(1<<7):
 values=[bool(flags&(1<<bit)) for bit in (0,1,2,4,3,5)]
 got=port.launcher_should_hover(*values);expected=ref.reference_hover(flags)
 assert bool(got)==bool(expected),(flags,got,expected)
print('Upstream comparison: 262144 hide states and 128 hover states match the original C++ functions.')
