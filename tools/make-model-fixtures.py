#!/usr/bin/env python3
"""Tiny standard GLBs covering static/mixed meshes and blended joint weights."""
import json, struct
from pathlib import Path
out=Path(__file__).resolve().parent.parent/'tests/fixtures'
data=bytearray();views=[];accessors=[]
def acc(rows,kind,fmt='f',target=None):
    while len(data)%4:data.append(0)
    flat=[v for row in rows for v in row];raw=struct.pack('<'+fmt*len(flat),*flat)
    view={'buffer':0,'byteOffset':len(data),'byteLength':len(raw)}
    if target:view['target']=target
    views.append(view);data.extend(raw)
    a={'bufferView':len(views)-1,'componentType':5126 if fmt=='f' else 5123,'count':len(rows),'type':kind}
    if kind in ('VEC3','SCALAR'):a.update(min=[min(x) for x in zip(*rows)],max=[max(x) for x in zip(*rows)])
    accessors.append(a);return len(accessors)-1
pos=acc([(0,0,0),(1,0,0),(0,1,0)],'VEC3',target=34962)
normal=acc([(0,0,1)]*3,'VEC3',target=34962)
uv=acc([(0,0),(1,0),(0,1)],'VEC2',target=34962)
joints=acc([(0,1,0,0)]*3,'VEC4','H',34962)
weights=acc([(.25,.75,0,0)]*3,'VEC4',target=34962)
indices=acc([(0,),(1,),(2,)],'SCALAR','H',34963)
times=acc([(0,),(1,),(1.034,)],'SCALAR')
root=acc([(0,0,0),(.2,0,0),(.2,0,0)],'VEC3')
child=acc([(0,0,0),(0,.4,0),(0,.4,0)],'VEC3')
attributes={'POSITION':pos,'NORMAL':normal,'TEXCOORD_0':uv}
base={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0]}],
      'nodes':[{'name':'static','mesh':0}], 'meshes':[{'primitives':[{'attributes':attributes,'indices':indices}]}],
      'buffers':[{'byteLength':len(data)}],'bufferViews':views,'accessors':accessors}
def save(name):
    j=json.dumps(base,separators=(',',':')).encode();j+=b' '*((-len(j))%4);b=bytes(data)+b'\0'*((-len(data))%4)
    (out/name).write_bytes(struct.pack('<III',0x46546c67,2,28+len(j)+len(b))+struct.pack('<II',len(j),0x4e4f534a)+j+struct.pack('<II',len(b),0x004e4942)+b)
save('static.glb')
base['nodes']=[{'name':'root','children':[1]},{'name':'child'},{'name':'skinned','mesh':0,'skin':0},{'name':'static','mesh':1}]
base['scenes']=[{'nodes':[0,2,3]}]
base['skins']=[{'joints':[0,1]}]
base['meshes']=[{'primitives':[{'attributes':dict(attributes,JOINTS_0=joints,WEIGHTS_0=weights),'indices':indices}]},base['meshes'][0]]
base['animations']=[{'name':'Translate','samplers':[{'input':times,'output':root},{'input':times,'output':child}], 'channels':[{'sampler':0,'target':{'node':0,'path':'translation'}},{'sampler':1,'target':{'node':1,'path':'translation'}}]}]
save('mixed-weighted.glb')
