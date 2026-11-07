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
pointer=(root/'upstream/launcher/PointerBarrier.cpp').read_text()
launcher=(root/'upstream/launcher/Launcher.cpp').read_text()
decay=(root/'upstream/launcher/Decaymulator.cpp').read_text()
reference='''#include <cmath>
#include <algorithm>
#include <X11/extensions/XInput2.h>
class Decaymulator { public:
 int value=0, rate_of_decay=1500;
 struct Timer { void reset() {} } decay_timer_;
 bool OnDecayTimeout();
};
'''+function(decay,'bool Decaymulator::OnDecayTimeout')+'''
class LauncherHideMachine { public:
'''+enums(hh)+'''
HideMode _mode=HIDE_NEVER; HideQuirk _quirks=DEFAULT; bool _should_hide=false;
int reveal_pressure=2000, edge_decay_rate=1500;
Decaymulator decaymulator_;
void SetShouldHide(bool value,bool) { _should_hide=value; }
void EnsureHideState(bool skip_delay);
bool GetQuirk(HideQuirk,bool allow_partial=true) const;
void SetQuirk(HideQuirk quirk, bool active) {
 _quirks=static_cast<HideQuirk>(active ? (_quirks|quirk) : (_quirks&~quirk));
 EnsureHideState(true);
}
void AddRevealPressure(int pressure);
};
'''+hide[hide.index('#define VISIBLE_REQUIRED'):hide.index('void LauncherHideMachine::EnsureHideState')]+function(hide,'LauncherHideMachine::EnsureHideState')+'\n'+function(hide,'LauncherHideMachine::GetQuirk')+'\n'+function(hide,'LauncherHideMachine::AddRevealPressure')+'''
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
reference+='''
struct Options {
 float responsiveness=2;
 float edge_responsiveness() { return responsiveness; }
 int edge_decay_rate() { return 1500; }
 int edge_reveal_pressure() { return 2000; }
};
class Launcher { public:
 Options opts;
 LauncherHideMachine hide_machine_;
 Options* options() { return &opts; }
 void ConfigureBarrier();
};
'''+function(launcher,'void Launcher::ConfigureBarrier')+'\n'+function(pointer,'int GetEventVelocity')+'''
class PointerBarrierWrapper { public:
 int smoothing_count_=0, smoothing_accum_=0, emitted=0;
 float max_velocity_multiplier=2;
 void SendBarrierEvent(int,int,int velocity,int) { emitted=velocity; }
 void EmitCurrentData(int event_id,int x,int y);
};
'''+function(pointer,'PointerBarrierWrapper::EmitCurrentData')+'''
extern "C" int reference_velocity(double dx,double dy,unsigned millis) {
 XIBarrierEvent event{}; event.dx=dx;event.dy=dy;event.dtime=millis;
 return GetEventVelocity(&event);
}
extern "C" int reference_smoothed(int accum,int count,float responsiveness) {
 PointerBarrierWrapper wrapper;
 wrapper.smoothing_accum_=accum;wrapper.smoothing_count_=count;
 wrapper.max_velocity_multiplier=responsiveness;wrapper.EmitCurrentData(1,0,100);
 return wrapper.emitted;
}
extern "C" void reference_reveal(float responsiveness,const int* pressure,
 const int* ticks,int count,int* output) {
 Launcher launcher; launcher.opts.responsiveness=responsiveness;launcher.ConfigureBarrier();
 auto& m=launcher.hide_machine_;
 m._mode=LauncherHideMachine::AUTOHIDE;m._quirks=LauncherHideMachine::LAUNCHER_HIDDEN;
 m.EnsureHideState(true);
 m.decaymulator_.rate_of_decay=m.edge_decay_rate;
 for(int i=0;i<count;i++) {
  for(int tick=0;tick<ticks[i];tick++) m.decaymulator_.OnDecayTimeout();
  m.AddRevealPressure(pressure[i]);
  output[i*5]=m.reveal_pressure;output[i*5+1]=m.edge_decay_rate;
  output[i*5+2]=m.decaymulator_.value;output[i*5+3]=m._quirks;
  output[i*5+4]=m._should_hide;
 }
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

ref.reference_velocity.argtypes=[c.c_double,c.c_double,c.c_uint]
port.launcher_barrier_velocity.argtypes=ref.reference_velocity.argtypes
ref.reference_smoothed.argtypes=[c.c_int,c.c_int,c.c_float]
port.launcher_barrier_smoothed.argtypes=ref.reference_smoothed.argtypes
port.launcher_reveal_configure.argtypes=[c.POINTER(Hide),c.c_float]
port.hide_machine_pressure.argtypes=[c.POINTER(Hide),c.c_int,c.c_int64]
port.hide_machine_tick.argtypes=[c.POINTER(Hide),c.c_int64]
ref.reference_reveal.argtypes=[c.c_float,c.POINTER(c.c_int),c.POINTER(c.c_int),c.c_int,c.POINTER(c.c_int)]
for dx,dy in [(0,0),(-1,0),(0,3),(-4,3),(100,-200),(.125,.25)]:
 for millis in [0,1,8,16,75,1000]:
  assert port.launcher_barrier_velocity(dx,dy,millis)==ref.reference_velocity(dx,dy,millis)
traces=[([0]*20,[10]*20),([100,200,400,800,1200,1],[0,1,3,7,0,100]),
        ([600]*30,[7]*30),([100]*30,[100]*30),([2000,1,0,5000],[0,0,1,100])]
comparisons=0
for step in range(201):
 responsiveness=c.c_float(.2+7.8*step/200).value
 for accum,count in [(0,0),(1,1),(1000,3),(100000,100),(6500,1),(1999,2)]:
  assert port.launcher_barrier_smoothed(accum,count,responsiveness)==ref.reference_smoothed(accum,count,responsiveness)
 for pressures,ticks in traces:
  length=len(pressures);array=c.c_int*length;output=(c.c_int*(length*5))()
  ref.reference_reveal(responsiveness,array(*pressures),array(*ticks),length,output)
  m=Hide();port.hide_machine_init(c.byref(m));m.quirks=1
  port.hide_machine_mode(c.byref(m),1,0)
  port.launcher_reveal_configure(c.byref(m),responsiveness)
  now=0
  for i,(pressure,elapsed) in enumerate(zip(pressures,ticks)):
   now+=elapsed*10
   port.hide_machine_tick(c.byref(m),now)
   port.hide_machine_pressure(c.byref(m),pressure,now)
   actual=[m.reveal_pressure,m.decay_rate,m.pressure,m.quirks,m.should_hide]
   expected=list(output[i*5:i*5+5])
   assert actual==expected,(responsiveness,i,actual,expected)
   comparisons+=1
print(f'Reveal comparison: velocity, smoothing, and {comparisons} pressure/decay steps across 201 responsiveness values match C++.')
