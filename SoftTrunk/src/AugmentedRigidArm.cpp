// Copyright 2018 ...
#include "AugmentedRigidArm.h"

//#include <boost/filesystem.hpp>

AugmentedRigidArm::AugmentedRigidArm(bool is_create_xacro) {
    rbdl_check_api_version (RBDL_API_VERSION);
    for (int i = 0; i < NUM_ELEMENTS; ++i) {
        lengths.push_back(0.12);
        
    }
    masses.push_back(0.15);
    masses.push_back(0.14);
    masses.push_back(0.13);
    if (is_create_xacro){
        create_xacro();
        return;
    }
  create_rbdl_model();
  if (USE_ROS){
//      https://stackoverflow.com/questions/50324348/can-a-ros-node-be-created-outside-a-catkin-workspace
//      ros::init("", "", "joint_pub");
//      ros::NodeHandle n;
  }
}

void AugmentedRigidArm::create_xacro(){
    std::cout << "generating XACRO file robot.urdf.xacro...";
    std::ofstream xacro_file;
    //std::string pathToUse1 = "./urdf";
    //boost::filesystem::path dir1(pathToUse1);


  //if (!(boost::filesystem::exists(dir1))) {
  //    std::cout << "ERROR: following path doesn't exist" << std::endl;
  //    return;
  //}
    xacro_file.open("./urdf/robot.urdf.xacro");

    // write outlog text to the file
    xacro_file << "<?xml version='1.0'?><robot xmlns:xacro='http://www.ros.org/wiki/xacro' name='robot'>"<<
    "<xacro:include filename='macro_definitions.urdf.xacro' /><xacro:empty_link name='base_link'/>";

    // write out first PCC element
    // this is written outside for loop because parent of first PCC element must be called base_link
    xacro_file << "<xacro:PCC id='0' parent='base_link' child='mid-0' length='"<< lengths[0] << "' mass='" << masses[0] <<"'/>"<<
    "<xacro:empty_link name='mid-0'/>";
    // iterate over all the other PCC elements
    for (int i = 1; i < NUM_ELEMENTS; ++i) {
        xacro_file << "<xacro:PCC id='"<< i <<"' parent='"<< "mid-" << i-1 << "' child='"<< "mid-" << i <<"' length='"<< lengths[i] <<"' mass='"<< masses[i] <<"'/>"<<
        "<xacro:empty_link name='"<< "mid-" << i <<"'/>";
    }
    xacro_file << "</robot>";

    xacro_file.close();
    std::cout << "Finished generation. Run ./create_urdf in /urdf directory to generate robot.urdf from robot.urdf.xacro.\n";
}

void AugmentedRigidArm::create_rbdl_model() {
    rbdl_model = new RigidBodyDynamics::Model();
    if (!RigidBodyDynamics::Addons::URDFReadFromFile("./urdf/robot.urdf", rbdl_model, false)) {
        std::cerr << "Error loading model ./urdf/robot.urdf" << std::endl;
        abort();
      }
      rbdl_model->gravity = Vector3d(0., 0., 9.81);
      std::cout << "Robot model created, with " << rbdl_model->dof_count << " DoF. \n";
}

void AugmentedRigidArm::update(Vector2Nd q, Vector2Nd dq) {
    double phi;
    double theta;
    double L;
    double shrunk_L;
    double dshrunk_L;
    double ddshrunk_L;
    // first update xi (augmented model parameters)
    for (int i = 0; i < NUM_ELEMENTS; ++i) {
      // sanitize values to the defined range
        phi = q(2*i+0);
        theta = q(2*i+1);
        L= lengths[i];
        if (theta < 0){
          phi += PI;
          theta = -theta;
        }
        phi = fmod(phi, PI*2);
        // problems when theta is too close to zero?
        if (theta < 0.001)
          theta = 0.001;
       

        // construct the configuration(that is consistent with URDF/XACRO) for rigid arm
        shrunk_L = L*sin(theta/2)/(theta/2);
        xi(8*i+0,0) = phi;
        xi(8*i+1,0) = theta/2;
        xi(8*i+2,0) = (L-shrunk_L)/2;
        xi(8*i+3,0) = -phi;
        xi(8*i+4,0) = phi;
        xi(8*i+5,0) = (L-shrunk_L)/2;
        xi(8*i+6,0) = theta/2;
        xi(8*i+7,0) = -phi;

        // next, update the Jacobian Jm
        dshrunk_L = L * (theta*cos(theta/2)-2*sin(theta/2)) / (theta*theta);        
        Jm(8*i+0,2*i+0) = 1;
        Jm(8*i+1,2*i+1) = 0.5;
        Jm(8*i+2,2*i+1) = -dshrunk_L/2;
        Jm(8*i+3,2*i+0) = -1;
        Jm(8*i+4,2*i+0) = 1;
        Jm(8*i+5,2*i+1) = -dshrunk_L/2;
        Jm(8*i+6,2*i+1) = 0.5;
        Jm(8*i+7,2*i+0) = -1;

        // next, update dJm.
        ddshrunk_L = L * dq(2*i+1) * (4*sin(theta/2)/pow(theta,3) - 2*cos(theta/2)/pow(theta, 2) - sin(theta/2)/(2*theta));
        dJm(8*i+2, 2*i+1) = -ddshrunk_L/2;
        dJm(8*i+5, 2*i+1) = -ddshrunk_L/2;
    }

    extract_B_G();
}

void AugmentedRigidArm::extract_B_G() {
    // the fun part- extracting the B_xi(inertia matrix) and G_xi(gravity) from RBDL
    
    // first run ID with dQ and ddQ as zero vectors (gives gravity vector)
    VectorNd dQ_zeros = VectorNd::Zero(NUM_ELEMENTS*8);
    VectorNd ddQ_zeros = VectorNd::Zero(NUM_ELEMENTS*8);
    VectorNd tau = VectorNd::Zero(NUM_ELEMENTS*8);
    InverseDynamics(*rbdl_model, xi, dQ_zeros, ddQ_zeros, tau);
    G_xi = tau;
    
    // next, iterate through by making ddQ_zeros a unit vector and get inertia matrix
    for (int i = 0; i < NUM_ELEMENTS*8; ++i) {
        for (int j = 0; j < NUM_ELEMENTS * 8; ++j) {
            ddQ_zeros(j) = 0.0;
        }
        ddQ_zeros(i) = 1.0;
        InverseDynamics(*rbdl_model, xi, dQ_zeros, ddQ_zeros, tau);
        B_xi.col(i) = tau - G_xi;
    }
}

void AugmentedRigidArm::joint_publish(){

}
