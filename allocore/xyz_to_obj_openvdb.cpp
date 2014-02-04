

#include "allocore/io/al_App.hpp"
#include "AlloSystem/allocv/allocv/al_OpenCV.hpp"
#include "alloutil/al_TextureGL.hpp"
#include "alloutil/al_FrameBufferGL.hpp"


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <assert.h>


#include <openvdb/openvdb.h>
#include <openvdb/tools/ValueTransformer.h>

#include <openvdb/tools/LevelSetUtil.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/Exceptions.h>
#include <openvdb/tree/LeafManager.h>
#include <openvdb/util/Util.h>
#include <openvdb/tools/ParticlesToLevelSet.h>
#include <openvdb/tools/LevelSetFilter.h>

#include <openvdb/tools/PointScatter.h>
#include <openvdb/math/Operators.h>


using namespace al;

/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
// this comes from openvdb2.1 TestParticlesToLevelSet.cc

class MyParticleList
{
protected:
    struct MyParticle {
        openvdb::Vec3R p, v;
        openvdb::Real  r;
    };
    openvdb::Real           mRadiusScale;
    openvdb::Real           mVelocityScale;
    std::vector<MyParticle> mParticleList;

public:
    MyParticleList(openvdb::Real rScale=1, openvdb::Real vScale=1)
        : mRadiusScale(rScale), mVelocityScale(vScale) {}
    void add(const openvdb::Vec3R &p, const openvdb::Real &r,
             const openvdb::Vec3R &v=openvdb::Vec3R(0,0,0))
    {
        MyParticle pa;
        pa.p = p;
        pa.r = r;
        pa.v = v;
        mParticleList.push_back(pa);
    }

    void remove()
    {
        mParticleList.pop_back();
    }

    /// @return coordinate bbox in the space of the specified transfrom
    openvdb::CoordBBox getBBox(const openvdb::GridBase& grid) {
        openvdb::CoordBBox bbox;
        openvdb::Coord &min= bbox.min(), &max = bbox.max();
        openvdb::Vec3R pos;
        openvdb::Real rad, invDx = 1/grid.voxelSize()[0];
        for (size_t n=0, e=this->size(); n<e; ++n) {
            this->getPosRad(n, pos, rad);
            const openvdb::Vec3d xyz = grid.worldToIndex(pos);
            const openvdb::Real   r  = rad * invDx;
            for (int i=0; i<3; ++i) {
                min[i] = openvdb::math::Min(min[i], openvdb::math::Floor(xyz[i] - r));
                max[i] = openvdb::math::Max(max[i], openvdb::math::Ceil( xyz[i] + r));
            }
        }
        return bbox;
    }
    //typedef int AttributeType;
    // The methods below are only required for the unit-tests
    openvdb::Vec3R pos(int n)   const {return mParticleList[n].p;}
    openvdb::Vec3R vel(int n)   const {return mVelocityScale*mParticleList[n].v;}
    openvdb::Real radius(int n) const {return mRadiusScale*mParticleList[n].r;}

    //////////////////////////////////////////////////////////////////////////////
    /// The methods below are the only ones required by tools::ParticleToLevelSet
    /// @note We return by value since the radius and velocities are modified
    /// by the scaling factors! Also these methods are all assumed to
    /// be thread-safe.

    /// Return the total number of particles in list.
    ///  Always required!
    size_t size() const { return mParticleList.size(); }

    void setPos(size_t n,  openvdb::Vec3R&pos) { 
      mParticleList[n].p = pos;
    }

    void setRad(size_t n,  openvdb::Real& rad) {
      mParticleList[n].r = rad ;
    }

    /// Get the world space position of n'th particle.
    /// Required by ParticledToLevelSet::rasterizeSphere(*this,radius).
    void getPos(size_t n,  openvdb::Vec3R&pos) const { pos = mParticleList[n].p; }

    
    void getPosRad(size_t n,  openvdb::Vec3R& pos, openvdb::Real& rad) const {
        pos = mParticleList[n].p;
        rad = mRadiusScale*mParticleList[n].r;
    }
    void getPosRadVel(size_t n,  openvdb::Vec3R& pos, openvdb::Real& rad, openvdb::Vec3R& vel) const {
        pos = mParticleList[n].p;
        rad = mRadiusScale*mParticleList[n].r;
        vel = mVelocityScale*mParticleList[n].v;
    }
    // The method below is only required for attribute transfer
    void getAtt(size_t n, openvdb::Index32& att) const { att = n; }
};


/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
// this is from openvdb forums and is built to read obj files that are Tri or Quad meshes
// I edited it to read .xyz files which are just vertices in plain text seperated by spaces

void split(const std::string& in, std::vector<std::string>& result){
  
  size_t pos = 0;
  size_t ndx = in.find(' ');
  
  while( true ){
  
    if( in[pos] != ' ' )
      result.push_back( in.substr(pos, ndx - pos) );

    if( ndx == std::string::npos ) break;
    pos = ndx+1;
    ndx = in.find(' ', pos);
  }
}

/////////////////////////////////////////////////////////////////////////////////////

void ReadMesh(const char* filename, std::vector<openvdb::Vec3R>& verts ){//, 
   /// std::vector<openvdb::Vec3s>& verts,
  //  std::vector<openvdb::Vec4I>& polys){
  
  std::ifstream file;
  file.open(filename, std::ifstream::in);

  std::string line;
  std::vector<std::string> vals;
  while(getline(file, line)){

    vals.clear();
    split(line, vals);

    // vertex
   /* if( line[0] == 'v' ){
      if( vals.size() != 4 ){
        fprintf(stderr, "problem with string: %s\n", line.c_str());
        exit(1);
      }
      */
      verts.push_back( openvdb::Vec3s(
        atof(vals[0].c_str()), atof(vals[1].c_str()),
        atof(vals[2].c_str())) );
  /*  }
    
    // face, using verticies in ->INDEX SPACE ?<-
    else if( line[0] == 'f' ){
      if( vals.size() != 4 && vals.size() != 5 ){
        fprintf(stderr, "problem with string: %s\n", line.c_str());
        exit(1);
      }
      
      else if( vals.size() == 4 ){
        polys.push_back( openvdb::Vec4I(
          atoi(vals[1].c_str()), atoi(vals[2].c_str()),
          atoi(vals[3].c_str()), openvdb::util::INVALID_IDX) );
      }

      else{
        polys.push_back( openvdb::Vec4I(
          atoi(vals[1].c_str()), atoi(vals[2].c_str()),
          atoi(vals[3].c_str()), atoi(vals[4].c_str())) );
      }
      */
    }
  //}
  
  file.close();
}
/////////////////////////////////////////////////////////////////////////////////////
// this is from openvdb forums - 
// I'm not using it right now

void WriteMesh(const char* filename,
    openvdb::tools::VolumeToMesh &mesh ){
  
  std::ofstream file;
  file.open(filename);
  
  openvdb::tools::PointList *verts = &mesh.pointList();
  openvdb::tools::PolygonPoolList *polys = &mesh.polygonPoolList();
  
  for( size_t i = 0; i < mesh.pointListSize(); i++ ){
    openvdb::Vec3s &v = (*verts)[i];
    file << "v " << v[0] << " " << v[1] << " " << v[2] << std::endl;
  }

  for( size_t i = 0; i < mesh.polygonPoolListSize(); i++ ){
  
    for( size_t ndx = 0; ndx < (*polys)[i].numTriangles(); ndx++ ){
      openvdb::Vec3I *p = &((*polys)[i].triangle(ndx));
      file << "f " << p->x() << " " << p->y() << " " << p->z() << std::endl;
    }

    for( size_t ndx = 0; ndx < (*polys)[i].numQuads(); ndx++ ){
      openvdb::Vec4I *p = &((*polys)[i].quad(ndx));
      file << "f " << p->x() << " " << p->y() << " " 
                   << p->z() << " " << p->w() << std::endl;
    }
  }

  file.close();
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////

struct AlloApp : App {

  bool shouldsolid = true;
  // behavioral variables
  bool meshisbusy = 0;
  bool shouldchange = false;
  // opengl and 3d allcore variables
  Light light;
  Mesh editingmesh;
  Mesh readymesh;
  Mesh LoadedMesh;
  Material material;
  Lens lens;

  MyParticleList pa;

  // radius should be about the distance between each vertex 
  // voxel size should be this size or smaller
  // half width should be double the voxel size or bigger

  float startingradius = 0.1f; 
  float voxelSize = 0.1f; //0.025f;//0.25f;
  float halfWidth = 0.2f;//1.0f;
  float particleRadiusShift = 0.00;  //this is used to adjust the particle size while app is running, dont adjust this here


  virtual Mesh VDBtoAllo(openvdb::FloatGrid::Ptr grid)//, Mesh NewMesh, std::string Boof)
  {

        using openvdb::Index64;

        openvdb::tools::VolumeToMesh mesher(
            grid->getGridClass() == openvdb::GRID_LEVEL_SET ? 0.0 : 0.01);
        mesher(*grid);

        Mesh NewMesh;//to be returned when function ends
        //NewMesh.reset();
        NewMesh.primitive(Graphics::QUADS);


        // the size of this vector is multiplied by 3 because each mesher.pointlist() is three floating point numbers
        //std::vector<GLfloat> points(mesher.pointListSize() * 3);
          std::vector<GLfloat> normals(mesher.pointListSize() * 3);

        // this has something to do with generating normals
        //openvdb::tree::ValueAccessor<const typename GridType::TreeType> acc(grid->tree());
        
        openvdb::tree::ValueAccessor<const openvdb::FloatTree> acc(grid->tree());
        typedef openvdb::math::Gradient<openvdb::math::GenericMap, openvdb::math::CD_2ND> Gradient;
        openvdb::math::GenericMap map(grid->transform());
        openvdb::Coord ijk;
        
        // Copy points into a vector of floating point numbers
        for (Index64 n = 0, i = 0, N = mesher.pointListSize(); n < N; ++n) {
            const openvdb::Vec3s& p = mesher.pointList()[n];
           
            NewMesh.vertex(p[0],p[1],p[2]);
        }

        // same number of normals as vertices
        NewMesh.normals().size( NewMesh.vertices().size() );

         // Copy primitives
              openvdb::tools::PolygonPoolList& polygonPoolList = mesher.polygonPoolList();
              Index64 numQuads = 0;
              for (Index64 n = 0, N = mesher.polygonPoolListSize(); n < N; ++n) {
                  numQuads += polygonPoolList[n].numQuads();
              }
             // std::cout << "number of faces "  << numQuads  << " number of verts " << mesher.pointListSize() << std::endl;
              //std::vector<GLuint> indices;
              //indices.reserve(numQuads * 4);
              openvdb::Vec3d normal, e1, e2;


              for (Index64 n = 0, N = mesher.polygonPoolListSize(); n < N; ++n) {

                  const openvdb::tools::PolygonPool& polygons = polygonPoolList[n];

                  for (Index64 i = 0, I = polygons.numQuads(); i < I; ++i) {
                      const openvdb::Vec4I& quad = polygons.quad(i);
                      
                      NewMesh.index(quad[0]);
                      NewMesh.index(quad[1]);
                      NewMesh.index(quad[2]);
                      NewMesh.index(quad[3]);
                      
                      e1 = mesher.pointList()[quad[1]];
                      e1 -= mesher.pointList()[quad[0]];
                      e2 = mesher.pointList()[quad[2]];
                      e2 -= mesher.pointList()[quad[1]];
                      normal = e1.cross(e2);

                      const double length = normal.length();
                      if (length > 1.0e-7){
                        normal *= (1.0 / length);
                      }
                      
                      //NewMesh.normals()[quad[0]]

                       al::Vec3f Mynormal;
                       Mynormal = al::Vec3f(-normal[0],-normal[1],-normal[2]);

                       NewMesh.normals()[quad[0]] += Mynormal;
                       NewMesh.normals()[quad[0]].normalize();
                       
                       NewMesh.normals()[quad[1]] += Mynormal;
                       NewMesh.normals()[quad[1]].normalize();

                       NewMesh.normals()[quad[2]] += Mynormal;
                       NewMesh.normals()[quad[2]].normalize();

                       NewMesh.normals()[quad[3]] += Mynormal;
                       NewMesh.normals()[quad[3]].normalize();
                }
            }

  return NewMesh;

  }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  AlloApp() {

    //StartCV();

    nav().pos(0, 0, 8);
    light.pos(0, 0, 8);
    editingmesh.primitive(Graphics::QUADS);
    readymesh.primitive(Graphics::QUADS);
    LoadedMesh.primitive(Graphics::QUADS);



    initWindow(Window::Dim(644,784), "X", 10);
    initAudio();


  // extract the verts and faces from object file
  std::vector<openvdb::Vec3R> verts;
  std::vector<openvdb::Vec4I> polys;
  ReadMesh("6m_cropped.xyz",verts);
  //ReadMesh("100k.obj", verts, polys);
  std::cout << "points: " << verts.size()
            << " polys: " << polys.size() << std::endl;

  // add particles where all the vertices are

  for(int i =0; i< verts.size();i++){
    openvdb::Vec3R pos = openvdb::Vec3R( verts[i] );
    openvdb::Real rad = openvdb::Real(startingradius );
    openvdb::Vec3R vel =openvdb::Vec3R(0.0f); //openvdb::Vec3R(al::rnd::uniformS(),al::rnd::uniformS(),al::rnd::uniformS()) ;

    pa.add(
        pos,
        rad,
        vel
    );

  }


    openvdb::FloatGrid::Ptr ls = openvdb::createLevelSet<openvdb::FloatGrid>(voxelSize, halfWidth);
    openvdb::tools::ParticlesToLevelSet<openvdb::FloatGrid> raster(*ls);

    raster.setGrainSize(1);//a value of zero disables threading
    raster.rasterizeSpheres(pa);
    raster.finalize();

    openvdb::tools::LevelSetFilter<openvdb::FloatGrid> filter(*ls);
    filter.laplacian();

    editingmesh = VDBtoAllo(ls);
    editingmesh.unitize();
    readymesh = editingmesh;




  }  // end alloapp ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  virtual void onAnimate(al_sec dt) {
  
  if(shouldchange==true){
    openvdb::Real rad;
    openvdb::Vec3R pos;
    pa.getPosRad(0,pos,rad);
    std::cout << rad << "current radius" << std::endl;
  for(int i =0; i< pa.size();i++){
    meshisbusy = true;
    size_t id = i;
    openvdb::Real rad;
    openvdb::Vec3R pos;
    pa.getPosRad(id,pos,rad);
    rad += particleRadiusShift;
    //setPos(id,pos);
    pa.setRad(id,rad);  
  }
  meshisbusy = false;

  openvdb::FloatGrid::Ptr ls = openvdb::createLevelSet<openvdb::FloatGrid>(voxelSize, halfWidth);
  openvdb::tools::ParticlesToLevelSet<openvdb::FloatGrid> raster(*ls);

  raster.setGrainSize(1);//a value of zero disables threading
  raster.rasterizeSpheres(pa);
  raster.finalize();

  openvdb::tools::LevelSetFilter<openvdb::FloatGrid> filter(*ls);
  filter.laplacian();

  editingmesh = VDBtoAllo(ls);
  editingmesh.unitize();
  readymesh = editingmesh;
  std::cout << "active voxel count: " << ls->activeVoxelCount() << std::endl;
  std::cout << " number of verts " << readymesh.vertices().size() << std::endl;
  }
  shouldchange = false;
//std::cout << "active voxel count: " << ls->activeVoxelCount() << std::endl;
  }  // end onAnimate ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  virtual void onDraw(Graphics& g, const Viewpoint& v) {

    material();   
    light();
    g.draw(readymesh);


  }  // end onDraw ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  virtual void onSound(AudioIOData& io) {
    // this will make a pulse wave with a frequency of 44100 / 128 Hz
    // leftover from elsewhere....
    /*
    io();
    io.out(0) = io.out(1) = 1.0f; // left = right = 1.0f;
    while (io()) {
      io.out(0) = io.out(1) = 0.0f;
    }
    */
  }  // end onSound ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  virtual void onKeyDown(const ViewpointWindow&, const Keyboard& k) {
      
        if (k.key() == 'm') {
          if(shouldsolid==true){
            shouldsolid = false;
            readymesh.primitive(Graphics::LINE_LOOP);

          } else {
            shouldsolid = true;
            readymesh.primitive(Graphics::QUADS);
          } 

        }

        if (k.key() == ']') {
         particleRadiusShift -= 0.01;
          shouldchange = true;
         std::cout << particleRadiusShift << " particle radius decreased " << std::endl;

        }        

        if (k.key() == '[') {
         particleRadiusShift += 0.01;
          shouldchange = true;

         std::cout << particleRadiusShift << " particle radius increased " << std::endl;

        }
         if (k.key() == '=') {
          halfWidth += 0.05;
          std::cout << halfWidth << " halfWidth increased " << std::endl;
          shouldchange = true;
        }        

        if (k.key() == '-') {
          halfWidth -= 0.05;
          std::cout << halfWidth << " halfWidth decreased " << std::endl;
          shouldchange = true;
        }
         if (k.key() == ',') {
          voxelSize += 0.005;
          std::cout << voxelSize << " voxelSize increased " << std::endl;
          shouldchange = true;
        }        

        if (k.key() == '.') {
          voxelSize -= 0.005;
          std::cout << voxelSize << " voxelSize decreased " << std::endl;
          shouldchange = true;

        }

         if (k.key() == ' ') {
          Mesh::dumpAll("obj");
        }

  }  // end onKeyDown~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

 
};// end struct AlloApp App ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


int main(int argc, char* argv[]) {

  openvdb::initialize();
  AlloApp app;
  app.start();
   
  return 0;
}
