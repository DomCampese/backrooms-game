#!/usr/bin/env python3
"""One-time conversion of loafbrr_1's CC0 archive (requires numpy and Pillow).
Usage: python tools/import-revolver.py /path/to/unpacked/archive
Normal builds use the committed converted files and Python's stdlib only.
"""
import base64, json, struct, sys
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
 return result
# Keep all gun geometry plus live rounds. Spent-case alternatives and loose props
# are excluded, preventing duplicate overlapping cartridges from the source scene.
meshes=[]
for material in [1,0]:
 verts=[];indices=[]
 for node in p['nodes'][17:36]:
  if 'Fired' in node['name']:continue
  pr=p['meshes'][node['mesh']]['primitives'][0]
  if pr['material']!=material:continue
  a=pr['attributes'];positions=acc(a['POSITION']);normals=acc(a['NORMAL']);uv=acc(a['TEXCOORD_0']);joints=acc(a['JOINTS_0']);weights=acc(a['WEIGHTS_0'])
  assert np.allclose(weights,[1,0,0,0]),'Converter requires rigid skinning'
  offset=len(verts)
  for xyz,n,tex,j in zip(positions,normals,uv,joints):verts.append((*xyz,*n,*tex,int(j[0])))
  indices.extend(int(i[0])+offset for i in acc(pr['indices']))
 assert len(verts)<65536 and max(indices)<len(verts)
 meshes.append((verts,indices))
with (dest/'mesh.bin').open('wb') as f:
 for verts,indices in meshes:
  f.write(struct.pack('<II',len(verts),len(indices)))
  for v in verts:f.write(struct.pack('<8fI',*v))
  f.write(struct.pack('<'+'H'*len(indices),*indices))
 print('Meshes:',[(len(v),len(i)//3) for v,i in meshes])
# Bake skin matrices once at import. 30 Hz animation needs no glTF parser or
# skeleton traversal at runtime. Linear pose interpolation is limited to 1/30 s.
clips=[('Default',0),('Reload',1.1666666269),('Shoot',2.5/6)]
with (dest/'poses.bin').open('wb') as f:
 for name,duration in clips:
  anim=next(a for a in p['animations'] if a['name']==name);count=max(1,round(duration*30)+1)
  f.write(struct.pack('<If',count,duration))
  for time in np.linspace(0,duration,count):
   for matrix in pose(anim,time):f.write(matrix[:3,:].astype('<f4').tobytes())
for material,folder in [('gun','Revolver_1'),('ammo','RevolverAmmo')]:
 def img(suffix):return np.asarray(Image.open(source/'Textures'/folder/f'{folder}_{suffix}.png').convert('RGB').resize((512,512),Image.Resampling.LANCZOS),dtype=float)/255
 albedo=img('Albedo');ao=img('AO');normal=img('Normal');rough=img('Roughness');metal=img('Metalness')
 # The scene shader is diffuse-plus-gloss, not a metallic BRDF. Attenuate
 # metallic diffuse energy so authored silver doesn't become white paint.
 diffuse_scale=(1-.65*metal) if material=='gun' else (1-.30*metal)
 Image.fromarray(np.uint8(np.clip(albedo*diffuse_scale*(.65+.35*ao)*255,0,255))).save(dest/f'{material}.jpg',quality=92,optimize=True)
 detail=np.zeros((512,512,4),dtype=np.uint8);n=normal*2-1
 detail[:,:,:2]=np.uint8(np.clip(128-127*n[:,:,:2]/np.maximum(n[:,:,2:],.25),0,255))
 detail[:,:,2]=np.uint8(np.clip((1-rough[:,:,0])*(.18+.72*metal[:,:,0])*255,0,255));detail[:,:,3]=128
 Image.fromarray(detail).save(dest/f'{material}-detail.png',optimize=True)
