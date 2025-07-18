#ifndef SRC_GAZEBO_LIVOX_ODE_MULTIRAY_SHAPE_H
#define SRC_GAZEBO_LIVOX_ODE_MULTIRAY_SHAPE_H
#include <gazebo/physics/MultiRayShape.hh>
#include <gazebo/util/system.hh>
#include <gazebo/ode/common.h>
#include <ignition/math4/ignition/math.hh>

namespace gazebo{
namespace physics{
class GZ_PHYSICS_VISIBLE LivoxOdeMultiRayShape : public MultiRayShape{
    public: explicit LivoxOdeMultiRayShape(CollisionPtr _parent);

    public: virtual ~LivoxOdeMultiRayShape();

    public: virtual void UpdateRays();

    public: virtual void Init();

    public: std::vector<RayShapePtr> &RayShapes(){return rays;}

    private: static void UpdateCallback(void *_data, dGeomID _o1,
                                        dGeomID _o2);

    public: void AddRay(const ignition::math::Vector3d &_start,
                           const ignition::math::Vector3d &_end);

    private: dSpaceID superSpaceId;

    private: dSpaceID raySpaceId;

 private:
    std::vector<RayShapePtr> livoxRays;
};
}
}


#endif  
