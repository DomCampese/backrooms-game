#!/usr/bin/env python3
"""One-time conversion of loafbrr_1's CC0 archive (requires numpy and Pillow).
Usage: python tools/import-revolver.py /path/to/unpacked/archive
Normal builds use the committed converted files and Python's stdlib only.
"""
import base64, io, json, struct, sys
from pathlib import Path
import numpy as np
from PIL import Image
source=Path(sys.argv[1]); dest=Path(__file__).resolve().parent.parent/'assets/revolver'
p=json.loads((source/'GLTF/RevolverExport.gltf').read_text())
b=base64.b64decode(p['buffers'][0]['uri'].split(',')[1])
def acc(i):
 a=p['accessors'][i];v=p['bufferViews'][a['bufferView']];n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']];fmt={5126:'f',5123:'H',5121:'B'}[a['componentType']]
 return np.array([struct.unpack_from('<'+fmt*n,b,v.get('byteOffset',0)+a.get('byteOffset',0)+j*v.get('byteStride',struct.calcsize(fmt)*n)) for j in range(a['count'])])
def trs(t,q,s):
 x,y,z,w=q;return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),t[0]],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),t[1]],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),t[2]],[0,0,0,1]])@np.diag([*s,1])
def slerp(a,b,t):
 dot=np.dot(a,b)
 if dot<0:b=-b;dot=-dot
 if dot>.9995:r=a*(1-t)+b*t;return r/np.linalg.norm(r)
 theta=np.arccos(np.clip(dot,-1,1));return (a*np.sin((1-t)*theta)+b*np.sin(t*theta))/np.sin(theta)
parents={c:i for i,n in enumerate(p['nodes']) for c in n.get('children',[])}
skin=p['skins'][0];inv=acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
# Rotate source +X barrel to game +Z, retain handedness, center at the cylinder.
C=np.array([[0,0,-1,0],[0,1,0,-.04],[1,0,0,-.065],[0,0,0,1.]])
def pose(animation,time):
 values={}
 for ch in animation['channels']:
  sam=animation['samplers'][ch['sampler']];times=acc(sam['input'])[:,0];out=acc(sam['output']);i=max(0,min(len(times)-2,int(np.searchsorted(times,time)-1)))
  f=np.clip((time-times[i])/(times[i+1]-times[i]),0,1) if len(times)>1 else 0
  value=(slerp(out[i],out[i+1],f) if ch['target']['path']=='rotation' else out[i]*(1-f)+out[i+1]*f) if len(times)>1 else out[0]
  values[ch['target']['node'],ch['target']['path']]=value
 worlds={}
 def world(i):
  if i not in worlds:
   n=p['nodes'][i];local=trs(*[values.get((i,k),n.get(k,d)) for k,d in [('translation',[0,0,0]),('rotation',[0,0,0,1]),('scale',[1,1,1])]])
   worlds[i]=(world(parents[i])@local) if i in parents else local
  return worlds[i]
 result=[C@world(node)@inv[j] for j,node in enumerate(skin['joints'])]
 # Reverse the reload articulation in the handle's local frame. Conjugating
 # with two reflections reverses motion without mirroring the mesh/UVs or
 # winding, and preserves the closed pose. Cartridges follow the same change.
 if animation['name']=='Reload':
  handle=world(skin['joints'][0]);mirror=np.diag([1.,1.,-1.,1.])
  opposite=C@handle@mirror@np.linalg.inv(handle)
  for j in range(3,len(skin['joints'])):
   result[j]=opposite@world(skin['joints'][j])@inv[j]@mirror
 # Source shooting swaps one live-round mesh for a spent-case alternative.
 # Keep our cartridge batch attached to the drum through the shot; the closed
 # gun hides the tips. Reload retains the authored individual cartridge motion.
 if animation['name']=='Shoot':
  for joint in (7,8,9,14,15,16):result[joint]=result[4]
 # The source uses quarter-size live rounds as its visibility switch while
 # spent cases eject. Those tiny rounds became visible when the spent meshes
 # were omitted. Export explicit hidden poses, never miniature cartridges.
 for j,node in enumerate(skin['joints']):
  name=p['nodes'][node]['name']
  if not name.startswith('DEF_Bullet'):continue
  fired=name.startswith('DEF_BulletFired')
  scale=np.linalg.norm(result[j][:3,:3],axis=0).mean()
  hidden=(fired and (animation['name']!='Reload' or time>=.70)) or scale<.99
  if hidden:
   result[j]=np.diag([1e-6,1e-6,1e-6,1.])
 return result
# Keep the gun, live rounds and spent cases. Animation visibility above makes
# the two cartridge sets mutually exclusive; unrelated loose props are excluded.
meshes=[]
for material in [1,0]:
 verts=[];indices=[]
 for node in p['nodes'][17:36]:
  pr=p['meshes'][node['mesh']]['primitives'][0]
  if pr['material']!=material:continue
  a=pr['attributes'];positions=acc(a['POSITION']);normals=acc(a['NORMAL']);uv=acc(a['TEXCOORD_0']);joints=acc(a['JOINTS_0']);weights=acc(a['WEIGHTS_0'])
  assert np.allclose(weights,[1,0,0,0]),'Converter requires rigid skinning'
  offset=len(verts)
  for xyz,n,tex,j in zip(positions,normals,uv,joints):verts.append((*xyz,*n,*tex,int(j[0])))
  indices.extend(int(i[0])+offset for i in acc(pr['indices']))
 assert len(verts)<65536 and max(indices)<len(verts)
 meshes.append((verts,indices))
clips=[('Idle', 'Default',0),('Reload','Reload',1.1666666269),('Shoot','Shoot',2.5/6)]
prepared_albedo={}
for material,folder in [('gun','Revolver_1'),('ammo','RevolverAmmo')]:
 def img(suffix):return np.asarray(Image.open(source/'Textures'/folder/f'{folder}_{suffix}.png').convert('RGB').resize((512,512),Image.Resampling.LANCZOS),dtype=float)/255
 albedo=img('Albedo');ao=img('AO');metal=img('Metalness')
 diffuse_scale=(1-.65*metal) if material=='gun' else (1-.30*metal)
 stream=io.BytesIO()
 Image.fromarray(np.uint8(np.clip(albedo*diffuse_scale*(.65+.35*ao)*255,0,255))).save(stream,format='JPEG',quality=92,optimize=True)
 prepared_albedo[material]=stream.getvalue()

# Export a standard glTF 2.0 binary. Runtime uses raylib's unmodified loaders;
# this preparation script only preserves this asset's art edits and clip trims.
import io
out={'asset':{'version':'2.0','generator':'Backrooms revolver preparation'},
     'scene':0,'scenes':[{'nodes':list(range(19))}],
     'nodes':[{'name':p['nodes'][node]['name']} for node in skin['joints']],
     'meshes':[],'materials':[],'textures':[],'images':[],
     'skins':[{'joints':list(range(17))}], 'animations':[],
     'bufferViews':[],'accessors':[]}
binary=bytearray()
def view(data):
 while len(binary)%4:binary.append(0)
 i=len(out['bufferViews']);out['bufferViews'].append({'buffer':0,'byteOffset':len(binary),'byteLength':len(data)})
 binary.extend(data);return i
def accessor(values,kind='VEC3',ctype=5126):
 values=np.asarray(values,dtype={5126:'<f4',5123:'<u2',5121:'u1'}[ctype]);i=len(out['accessors'])
 a={'bufferView':view(values.tobytes()),'componentType':ctype,'count':len(values),'type':kind}
 if kind in ('SCALAR','VEC3'):
  shaped=values.reshape(len(values),-1);a.update(min=shaped.min(axis=0).tolist(),max=shaped.max(axis=0).tolist())
 out['accessors'].append(a);return i
def texture(data,mime):
 i=len(out['textures']);out['images'].append({'bufferView':view(data),'mimeType':mime});out['textures'].append({'source':i});return {'index':i}
def png(pixels):
 stream=io.BytesIO();Image.fromarray(pixels).save(stream,format='PNG',optimize=True);return stream.getvalue()
for part,((verts,indices),(name,folder)) in enumerate(zip(meshes,[('gun','Revolver_1'),('ammo','RevolverAmmo')])):
 v=np.asarray(verts);xyz=np.c_[v[:,:3],np.ones(len(v))]@C.T;normals=v[:,3:6]@C[:3,:3].T
 joints=np.zeros((len(v),4),dtype=np.uint16);joints[:,0]=v[:,8];weights=np.zeros((len(v),4));weights[:,0]=1
 attributes={'POSITION':accessor(xyz[:,:3]),'NORMAL':accessor(normals),'TEXCOORD_0':accessor(v[:,6:8],'VEC2'),
             'JOINTS_0':accessor(joints,'VEC4',5123),'WEIGHTS_0':accessor(weights,'VEC4')}
 out['meshes'].append({'name':name,'primitives':[{'attributes':attributes,'indices':accessor(indices,'SCALAR',5123),'material':part}]})
 out['nodes'].append({'name':name,'mesh':part,'skin':0})
 albedo=texture(prepared_albedo[name],'image/jpeg')
 def source_image(suffix):return np.asarray(Image.open(source/'Textures'/folder/f'{folder}_{suffix}.png').convert('RGB').resize((512,512),Image.Resampling.LANCZOS))
 normal=texture(png(source_image('Normal')),'image/png')
 mr=np.full((512,512,3),255,dtype=np.uint8);mr[:,:,1]=source_image('Roughness')[:,:,0];mr[:,:,2]=source_image('Metalness')[:,:,0]
 roughness=texture(png(mr),'image/png')
 out['materials'].append({'name':name,'pbrMetallicRoughness':{'baseColorTexture':albedo,'metallicRoughnessTexture':roughness,'metallicFactor':1,'roughnessFactor':1},'normalTexture':normal})
# Flatten the prepared rig into independent named joints with identity binds.
# This preserves the two-reflection reload edit as standard TRS animation.
def decompose(m):
 scale=np.linalg.norm(m[:3,:3],axis=0);r=m[:3,:3]/np.maximum(scale,1e-9)
 # Eigenvector quaternion conversion also handles near-180-degree rotations.
 a=r;K=np.array([[a[0,0]-a[1,1]-a[2,2],a[1,0]+a[0,1],a[2,0]+a[0,2],a[2,1]-a[1,2]],
 [a[1,0]+a[0,1],a[1,1]-a[0,0]-a[2,2],a[2,1]+a[1,2],a[0,2]-a[2,0]],
 [a[2,0]+a[0,2],a[2,1]+a[1,2],a[2,2]-a[0,0]-a[1,1],a[1,0]-a[0,1]],
 [a[2,1]-a[1,2],a[0,2]-a[2,0],a[1,0]-a[0,1],np.trace(a)]])/3
 _,e=np.linalg.eigh(K);q=e[:,-1]
 return m[:3,3],q,scale
for name,source_name,duration in clips:
 animation=next(a for a in p['animations'] if a['name']==source_name)
 times=np.linspace(0,duration,max(2,round(duration*120)+1))
 # Raylib 5.5 samples clips every 17 ms and truncates the last interval.
 # A short terminal hold keeps the authored endpoint available to its sampler.
 if duration>0:times=np.append(times,duration+.034)
 time_access=accessor(times,'SCALAR')
 samples=[[decompose(matrix@np.linalg.inv(C)) for matrix in pose(animation,min(t,duration))] for t in times]
 clip={'name':name,'channels':[],'samplers':[]}
 for joint in range(17):
  for k,(path,kind) in enumerate([('translation','VEC3'),('rotation','VEC4'),('scale','VEC3')]):
   values=np.array([frame[joint][k] for frame in samples])
   if path=='rotation':
    for i in range(1,len(values)):
     if np.dot(values[i-1],values[i])<0:values[i]*=-1
   sampler=len(clip['samplers']);clip['samplers'].append({'input':time_access,'output':accessor(values,kind),'interpolation':'LINEAR'})
   clip['channels'].append({'sampler':sampler,'target':{'node':joint,'path':path}})
 out['animations'].append(clip)
# Give Idle a tiny nonzero duration: duplicate timestamps are invalid glTF.
idle_time=out['accessors'][out['animations'][0]['samplers'][0]['input']]
vw=out['bufferViews'][idle_time['bufferView']];struct.pack_into('<f',binary,vw['byteOffset']+4,1/120);idle_time['max']=[1/120]
out['buffers']=[{'byteLength':len(binary)}]
encoded=json.dumps(out,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4);binary+=b'\0'*((-len(binary))%4)
glb=struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary
model_dir=dest.parent/'models';model_dir.mkdir(exist_ok=True);(model_dir/'revolver.glb').write_bytes(glb)
print('Standard GLB:',len(glb),'bytes')
