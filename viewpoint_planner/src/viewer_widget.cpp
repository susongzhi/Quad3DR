//==================================================
// viewer_widget.cpp
//
//  Copyright (c) 2016 Benjamin Hepp.
//  Author: Benjamin Hepp
//  Created on: Dec 6, 2016
//==================================================

// This file is adapted from OctoMap.
// Original Copyright notice.
/*
 * This file is part of OctoMap - An Efficient Probabilistic 3D Mapping
 * Framework Based on Octrees
 * http://octomap.github.io
 *
 * Copyright (c) 2009-2014, K.M. Wurm and A. Hornung, University of Freiburg
 * All rights reserved. License for the viewer octovis: GNU GPL v2
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 */

#include "viewer_widget.h"
#include <cmath>
#include <manipulatedCameraFrame.h>
#include <ait/utilities.h>
#include <ait/pose.h>

using namespace std;

ViewerWidget::ViewerWidget(const QGLFormat& format, ViewpointPlanner* planner, ViewerSettingsPanel* settings_panel, QWidget *parent)
    : QGLViewer(format, parent), planner_(planner),
      initialized_(false), settings_panel_(settings_panel),
      octree_(nullptr), sparse_recon_(nullptr),
      display_axes_(true), aspect_ratio_(-1) {
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);

    // Connect signals for settings panel
    connect(settings_panel_, SIGNAL(drawOctreeChanged(bool)), this, SLOT(setDrawOctree(bool)));
    connect(settings_panel_, SIGNAL(occupancyBinThresholdChanged(double)), this, SLOT(setOccupancyBinThreshold(double)));
    connect(settings_panel_, SIGNAL(colorFlagsChanged(uint32_t)), this, SLOT(setColorFlags(uint32_t)));
    connect(settings_panel_, SIGNAL(voxelAlphaChanged(double)), this, SLOT(setVoxelAlpha(double)));
    connect(settings_panel_, SIGNAL(drawFreeVoxelsChanged(bool)), this, SLOT(setDrawFreeVoxels(bool)));
    connect(settings_panel_, SIGNAL(displayAxesChanged(bool)), this, SLOT(setDisplayAxes(bool)));
    connect(settings_panel_, SIGNAL(drawSingleBinChanged(bool)), this, SLOT(setDrawSingleBin(bool)));
    connect(settings_panel_, SIGNAL(drawCamerasChanged(bool)), this, SLOT(setDrawCameras(bool)));
    connect(settings_panel_, SIGNAL(drawPlannedViewpointsChanged(bool)), this, SLOT(setDrawPlannedViewpoints(bool)));
    connect(settings_panel_, SIGNAL(drawSparsePointsChanged(bool)), this, SLOT(setDrawSparsePoints(bool)));
    connect(settings_panel_, SIGNAL(refreshTree(void)), this, SLOT(refreshTree(void)));
    connect(settings_panel_, SIGNAL(drawRaycastChanged(bool)), this, SLOT(setDrawRaycast(bool)));
    connect(settings_panel_, SIGNAL(captureRaycast(void)), this, SLOT(captureRaycast(void)));
    connect(settings_panel_, SIGNAL(useDroneCameraChanged(bool)), this, SLOT(setUseDroneCamera(bool)));
    connect(settings_panel_, SIGNAL(imagePoseChanged(ImageId)), this, SLOT(setImagePoseIndex(ImageId)));
    connect(settings_panel_, SIGNAL(plannedViewpointChanged(size_t)), this, SLOT(setPlannedViewpointIndex(size_t)));
    connect(settings_panel_, SIGNAL(minOccupancyChanged(double)), this, SLOT(setMinOccupancy(double)));
    connect(settings_panel_, SIGNAL(maxOccupancyChanged(double)), this, SLOT(setMaxOccupancy(double)));
    connect(settings_panel_, SIGNAL(minObservationsChanged(uint32_t)), this, SLOT(setMinObservations(uint32_t)));
    connect(settings_panel_, SIGNAL(maxObservationsChanged(uint32_t)), this, SLOT(setMaxObservations(uint32_t)));
    connect(settings_panel_, SIGNAL(minVoxelSizeChanged(double)), this, SLOT(setMinVoxelSize(double)));
    connect(settings_panel_, SIGNAL(maxVoxelSizeChanged(double)), this, SLOT(setMaxVoxelSize(double)));
    connect(settings_panel_, SIGNAL(minWeightChanged(double)), this, SLOT(setMinWeight(double)));
    connect(settings_panel_, SIGNAL(maxWeightChanged(double)), this, SLOT(setMaxWeight(double)));
    connect(settings_panel_, SIGNAL(renderTreeDepthChanged(size_t)), this, SLOT(setRenderTreeDepth(size_t)));
    connect(settings_panel_, SIGNAL(renderObservationThresholdChanged(size_t)), this, SLOT(setRenderObservationThreshold(size_t)));

    // Fill occupancy dropbox in settings panel
    float selected_occupancy_bin_threshold = octree_drawer_.getOccupancyBinThreshold();
    settings_panel_->initializeOccupancyBinThresholds(octree_drawer_.getOccupancyBins());
    settings_panel_->selectOccupancyBinThreshold(selected_occupancy_bin_threshold);

    std::vector<std::pair<std::string, uint32_t>> color_flags_uint;
    for (const auto& entry : VoxelDrawer::getAvailableColorFlags()) {
      color_flags_uint.push_back(std::make_pair(entry.first, static_cast<uint32_t>(entry.second)));
    }
    settings_panel_->initializeColorFlags(color_flags_uint);
    settings_panel_->selectColorFlags(color_flags_uint[0].second);

    octree_drawer_.setMinOccupancy(settings_panel_->getMinOccupancy());
    octree_drawer_.setMaxOccupancy(settings_panel_->getMaxOccupancy());
    octree_drawer_.setMinObservations(settings_panel_->getMinObservations());
    octree_drawer_.setMaxObservations(settings_panel_->getMaxObservations());
    octree_drawer_.setMinVoxelSize(settings_panel_->getMinVoxelSize());
    octree_drawer_.setMaxVoxelSize(settings_panel_->getMaxVoxelSize());
    octree_drawer_.setRenderTreeDepth(settings_panel_->getRenderTreeDepth());
    octree_drawer_.setRenderObservationThreshold(settings_panel_->getRenderObservationThreshold());
    octree_drawer_.setMinWeight(settings_panel_->getMinWeight());
    octree_drawer_.setMaxWeight(settings_panel_->getMaxWeight());

    // Timer for getting camera pose updates
//    camera_pose_timer_ = new QTimer(this);
//    camera_pose_timer_->setSingleShot(true);
//    connect(camera_pose_timer_, SIGNAL(timeout()), this, SLOT(onCameraPoseTimeout()));
//    connect(this, SIGNAL(cameraPoseTimeoutHandlerFinished()), this, SLOT(onCameraPoseTimeoutHandlerFinished()));
//    emit cameraPoseTimeoutHandlerFinished();
}

ViewerWidget::~ViewerWidget() {
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void ViewerWidget::init() {
    initialized_ = true;

    //    setHandlerKeyboardModifiers(QGLViewer::CAMERA, Qt::AltModifier);
    //    setHandlerKeyboardModifiers(QGLViewer::FRAME, Qt::NoModifier);
    //    setHandlerKeyboardModifiers(QGLViewer::CAMERA, Qt::ControlModifier);
    setMouseTracking(true);

    // Restore previous viewer state.
    restoreStateFromFile();
    std::cout << "QGLViewer.stateFilename: " <<stateFileName().toStdString() << std::endl;

    // Make camera the default manipulated frame.
    setManipulatedFrame(camera()->frame());
    // invert mousewheel (more like Blender)
    camera()->frame()->setWheelSensitivity(-1.0);

    // TODO
    camera()->setSceneCenter(qglviewer::Vec(0, 0, 0));
    camera()->setSceneRadius(50);

    setUseDroneCamera(settings_panel_->getUseDroneCamera());

    // background color defaults to white
    this->setBackgroundColor(QColor(255,255,255));
    this->qglClearColor(this->backgroundColor());

    initAxesDrawer();

    sparce_recon_drawer_.init();
    planned_viewpoint_drawer_.init();
    if (octree_ != nullptr) {
        showOctree(octree_);
    }
    if (sparse_recon_ != nullptr) {
        showSparseReconstruction(sparse_recon_);
    }

    std::cout << "zNear: " << camera()->zNear() << ", zFar: " << camera()->zFar() << std::endl;
}

void ViewerWidget::initAxesDrawer() {
  axes_drawer_.init();
  std::vector<OGLLineData> line_data;
  float axes_length = 5;
  OGLVertexData axes_origin(5, 5, 20);
  OGLVertexData axes_end_x(axes_origin);
  axes_end_x.x += axes_length;
  OGLVertexData axes_end_y(axes_origin);
  axes_end_y.y += axes_length;
  OGLVertexData axes_end_z(axes_origin);
  axes_end_z.z += axes_length;
  OGLColorData color_x(1, 0, 0, 1);
  OGLColorData color_y(0, 1, 0, 1);
  OGLColorData color_z(0, 0, 1, 1);
  line_data.emplace_back(OGLVertexDataRGBA(axes_origin, color_x), OGLVertexDataRGBA(axes_end_x, color_x));
  line_data.emplace_back(OGLVertexDataRGBA(axes_origin, color_y), OGLVertexDataRGBA(axes_end_y, color_y));
  line_data.emplace_back(OGLVertexDataRGBA(axes_origin, color_z), OGLVertexDataRGBA(axes_end_z, color_z));
  axes_drawer_.upload(line_data);
}

int ViewerWidget::heightForWidth(int w) const {
    if (aspect_ratio_ <= 0) {
        return -1;
    }
    return static_cast<int>(w / aspect_ratio_);
}

QSize ViewerWidget::sizeHint() const {
    if (aspect_ratio_ <= 0) {
        return QSize();
    }
    return QSize(width(), width() / aspect_ratio_);
}

void ViewerWidget::showOctree(const ViewpointPlanner::OccupancyMapType* octree) {
    octree_ = octree;
    if (!initialized_) {
        return;
    }

    // update viewer stat
    double minX, minY, minZ, maxX, maxY, maxZ;
    minX = minY = minZ = -10; // min bbx for drawing
    maxX = maxY = maxZ = 10;  // max bbx for drawing
    double sizeX, sizeY, sizeZ;
    sizeX = sizeY = sizeZ = 0.;
    size_t memoryUsage = 0;
    size_t num_nodes = 0;

    ait::Timer timer;
    // get map bbx
    double lminX, lminY, lminZ, lmaxX, lmaxY, lmaxZ;
    octree_->getMetricMin(lminX, lminY, lminZ);
    octree_->getMetricMax(lmaxX, lmaxY, lmaxZ);
    // transform to world coords using map origin
    octomap::point3d pmin(lminX, lminY, lminZ);
    octomap::point3d pmax(lmaxX, lmaxY, lmaxZ);
    lminX = pmin.x(); lminY = pmin.y(); lminZ = pmin.z();
    lmaxX = pmax.x(); lmaxY = pmax.y(); lmaxZ = pmax.z();
    // update global bbx
    if (lminX < minX) minX = lminX;
    if (lminY < minY) minY = lminY;
    if (lminZ < minZ) minZ = lminZ;
    if (lmaxX > maxX) maxX = lmaxX;
    if (lmaxY > maxY) maxY = lmaxY;
    if (lmaxZ > maxZ) maxZ = lmaxZ;
    double lsizeX, lsizeY, lsizeZ;
    // update map stats
    octree_->getMetricSize(lsizeX, lsizeY, lsizeZ);
    if (lsizeX > sizeX) sizeX = lsizeX;
    if (lsizeY > sizeY) sizeY = lsizeY;
    if (lsizeZ > sizeZ) sizeZ = lsizeZ;
    memoryUsage += octree_->memoryUsage();
    num_nodes += octree_->size();
    timer.printTiming("Computing octree metrics");

    refreshTree();

    setSceneBoundingBox(qglviewer::Vec(minX, minY, minZ), qglviewer::Vec(maxX, maxY, maxZ));
}

void ViewerWidget::showViewpointPath(const ViewpointPlanner::ViewpointPath& viewpoint_path) {
  if (!initialized_) {
      return;
  }

  // Make a copy of the viewpoint path so that we can later on access
  // viewpoints select by the user
  viewpoint_path_ = planner_->getViewpointPath();

  // Fill planned viewpoint dropbox in settings panel
  std::vector<std::pair<std::string, size_t>> planned_viewpoint_gui_entries;
  for (size_t i = 0; i < planner_->getViewpointPath().size(); ++i) {
    ViewpointPlanner::FloatType information = planner_->getViewpointPath()[i].information;
    std::string name = std::to_string(i) + " - " + std::to_string(information);
    planned_viewpoint_gui_entries.push_back(std::make_pair(name, i));
  }
  settings_panel_->initializePlannedViewpoints(planned_viewpoint_gui_entries);

  std::vector<ait::Pose> poses;
  for (const auto& path_entry : viewpoint_path_) {
    poses.push_back(path_entry.node->viewpoint.pose());
  }
  planned_viewpoint_drawer_.setCamera(sparse_recon_->getCameras().cbegin()->second);
  planned_viewpoint_drawer_.setViewpoints(poses);

  update();
}

void ViewerWidget::showSparseReconstruction(const SparseReconstruction* sparse_recon) {
    sparse_recon_ = sparse_recon;
    if (!initialized_) {
        return;
    }

    // Fill camera poses dropbox in settings panel
    std::vector<std::pair<std::string, ImageId>> pose_entries;
    for (const auto& image_entry : sparse_recon_->getImages()) {
        pose_entries.push_back(std::make_pair(image_entry.second.name(), image_entry.first));
    }
    settings_panel_->initializeImagePoses(pose_entries);

    sparce_recon_drawer_.setSparseReconstruction(sparse_recon_);

    update();
}

void ViewerWidget::refreshTree()
{
    if (octree_ != nullptr) {
        octree_drawer_.setOctree(octree_);
    }
    update();
}

void ViewerWidget::setDrawRaycast(bool draw_raycast) {
  octree_drawer_.setDrawRaycast(draw_raycast);
  update();
}

void ViewerWidget::captureRaycast()
{
  ait::Pose camera_pose = getCameraPose();
//  std::vector<std::pair<ViewpointPlanner::ConstTreeNavigatorType, float>> raycast_results = planner_->getRaycastHitVoxels(camera_pose);
  std::vector<ViewpointPlannerData::OccupiedTreeType::IntersectionResult> raycast_results = planner_->getRaycastHitVoxelsBVH(camera_pose);
  std::vector<std::pair<ViewpointPlannerData::OccupiedTreeType::IntersectionResult, float>> tmp;
  tmp.reserve(raycast_results.size());
  for (auto it = raycast_results.cbegin(); it != raycast_results.cend(); ++it) {
    float information = planner_->computeInformationScore(*it);
    tmp.push_back(std::make_pair(*it, information));
  }
  octree_drawer_.updateRaycastVoxels(tmp);
  update();
}

void ViewerWidget::resetView()
{
    this->camera()->setOrientation((float) -M_PI / 2.0f, (float) M_PI / 2.0f);
    this->showEntireScene();
    updateGL();
}

qglviewer::Vec ViewerWidget::eigenToQglviewer(const Eigen::Vector3d& eig_vec) const {
    return qglviewer::Vec(eig_vec(0), eig_vec(1), eig_vec(2));
}

Eigen::Vector3d ViewerWidget::qglviewerToEigen(const qglviewer::Vec& qgl_vec) const {
    Eigen::Vector3d eig_vec;
    eig_vec << qgl_vec.x, qgl_vec.y, qgl_vec.z;
    return eig_vec;
}

qglviewer::Quaternion ViewerWidget::eigenToQglviewer(const Eigen::Quaterniond& eig_quat) const {
    qglviewer::Quaternion qgl_quat(eig_quat.x(), eig_quat.y(), eig_quat.z(), eig_quat.w());
    return qgl_quat;
}

Eigen::Quaterniond ViewerWidget::qglviewerToEigen(const qglviewer::Quaternion& qgl_quat) const {
    Eigen::Quaterniond eig_quat;
    eig_quat.x() = qgl_quat[0];
    eig_quat.y() = qgl_quat[1];
    eig_quat.z() = qgl_quat[2];
    eig_quat.w() = qgl_quat[3];
    return eig_quat;
}

// Return camera pose (transformation from world to camera coordinate system)
ait::Pose ViewerWidget::getCameraPose() const {
    ait::Pose inv_pose;
    inv_pose.translation() = qglviewerToEigen(camera()->position());
    // Convert to OpenGL camera coordinate system (x is right, y is up, z is back)
    Eigen::AngleAxisd rotate_x_pi = Eigen::AngleAxisd(M_PI, Eigen::Vector3d::UnitX());
    inv_pose.quaternion() = qglviewerToEigen(camera()->orientation()) * rotate_x_pi;
    return inv_pose.inverse();
//    qglviewer::Vec translation = this->camera()->frame()->translation();
//    qglviewer::Quaternion quaternion = this->camera()->frame()->rotation();
//    quaternion.invert();
//    translation = - (quaternion * translation);
//    Eigen::Matrix4d camera_pose = Eigen::Matrix4d::Identity();
//    for (size_t i = 0; i < 4; ++i) {
//        for (size_t j = 0; j < 4; ++j) {
//            camera_pose(i, j) = quaternion.matrix()[j * 4 + i];
//        }
//    }
//    for (size_t i = 0; i < 3; ++i) {
//        camera_pose(i, 3) = translation.v_[i];
//    }
//    return camera_pose;
}

// Set camera pose (transformation from world to camera coordinate system)
void ViewerWidget::setCameraPose(const ait::Pose& pose) {
    ait::Pose inv_pose = pose.inverse();
    camera()->setPosition(eigenToQglviewer(inv_pose.translation()));
    // Convert to OpenGL camera coordinate system (x is right, y is up, z is back)
    Eigen::AngleAxisd rotate_x_pi = Eigen::AngleAxisd(M_PI, Eigen::Vector3d::UnitX());
    camera()->setOrientation(eigenToQglviewer(inv_pose.quaternion() * rotate_x_pi));
    update();
}

void ViewerWidget::setOccupancyBinThreshold(double occupancy_threshold)
{
//    std::cout << "Setting occupancy threshold to " << occupancy_threshold << std::endl;
    octree_drawer_.setOccupancyBinThreshold(occupancy_threshold);
    update();
}

void ViewerWidget::setColorFlags(uint32_t color_flags)
{
//    std::cout << "Setting color flags to " << color_flags << std::endl;
    octree_drawer_.setColorFlags(color_flags);
    update();
}

void ViewerWidget::setDrawFreeVoxels(bool draw_free_voxels)
{
    octree_drawer_.setDrawFreeVoxels(draw_free_voxels);
    update();
}

void ViewerWidget::setDisplayAxes(bool display_axes)
{
  display_axes_ = display_axes;
  update();
}

void ViewerWidget::setVoxelAlpha(double voxel_alpha) {
//    std::cout << "Setting voxel alpha to " << voxel_alpha << std::endl;
    octree_drawer_.setAlphaOccupied(voxel_alpha);
    update();
}

void ViewerWidget::setDrawSingleBin(bool draw_single_bin) {
    octree_drawer_.setDrawSingleBin(draw_single_bin);
    update();
}

void ViewerWidget::setDrawOctree(bool draw_octree) {
  octree_drawer_.setDrawOctree(draw_octree);
  update();
}

void ViewerWidget::setDrawCameras(bool draw_cameras) {
    sparce_recon_drawer_.setDrawCameras(draw_cameras);
    update();
}

void ViewerWidget::setDrawPlannedViewpoints(bool draw_planned_viewpoints) {
  planned_viewpoint_drawer_.setDrawCameras(draw_planned_viewpoints);
  update();
}

void ViewerWidget::setDrawSparsePoints(bool draw_sparse_points) {
    sparce_recon_drawer_.setDrawSparsePoints(draw_sparse_points);
    update();
}

void ViewerWidget::setUseDroneCamera(bool use_drone_camera) {
    if (use_drone_camera) {
        const PinholeCameraColmap& pinhole_camera = sparse_recon_->getCameras().cbegin()->second;
        double fy = pinhole_camera.getFocalLengthY();
        double v_fov = 2 * std::atan(pinhole_camera.height() / (2 * fy));
        camera()->setFieldOfView(v_fov);
        aspect_ratio_ = pinhole_camera.width() / static_cast<double>(pinhole_camera.height());
        updateGeometry();
        std::cout << "Setting camera FOV to " << (v_fov * 180 / M_PI) << " degrees" << std::endl;
        std::cout << "Resized window to " << width() << " x " << height() << std::endl;
    }
    else {
        double v_fov = M_PI / 4;
        camera()->setFieldOfView(v_fov);
        std::cout << "Setting camera FOV to " << (v_fov * 180 / M_PI) << std::endl;
    }
    update();
}

void ViewerWidget::setImagePoseIndex(ImageId image_id) {
    const ImageColmap& image = sparse_recon_->getImages().at(image_id);
    setCameraPose(image.pose());
    update();
}

void ViewerWidget::setPlannedViewpointIndex(size_t index) {
  setCameraPose(viewpoint_path_[index].node->viewpoint.pose());
  update();
}

void ViewerWidget::setMinOccupancy(double min_occupancy) {
  octree_drawer_.setMinOccupancy(min_occupancy);
  update();
}

void ViewerWidget::setMaxOccupancy(double max_occupancy) {
  octree_drawer_.setMaxOccupancy(max_occupancy);
  update();
}

void ViewerWidget::setMinObservations(uint32_t min_observations) {
  octree_drawer_.setMinObservations(min_observations);
  update();
}

void ViewerWidget::setMaxObservations(uint32_t max_observations) {
  octree_drawer_.setMaxObservations(max_observations);
  update();
}

void ViewerWidget::setMinVoxelSize(double min_voxel_size) {
  octree_drawer_.setMinVoxelSize(min_voxel_size);
  update();
}

void ViewerWidget::setMaxVoxelSize(double max_voxel_size) {
  octree_drawer_.setMaxVoxelSize(max_voxel_size);
  update();
}

void ViewerWidget::setMinWeight(double min_weight) {
  octree_drawer_.setMinWeight(min_weight);
  update();
}

void ViewerWidget::setMaxWeight(double max_weight) {
  octree_drawer_.setMaxWeight(max_weight);
  update();
}

void ViewerWidget::setRenderTreeDepth(size_t render_tree_depth)
{
//    std::cout << "Setting render tree depth to " << render_tree_depth << std::endl;
    octree_drawer_.setRenderTreeDepth(render_tree_depth);
    update();
}

void ViewerWidget::setRenderObservationThreshold(size_t render_observation_threshold)
{
//    std::cout << "Setting render observation threshold to " << observation_threshold << std::endl;
    octree_drawer_.setRenderObservationThreshold(render_observation_threshold);
    update();
}

void ViewerWidget::setSceneBoundingBox(const qglviewer::Vec& min, const qglviewer::Vec& max)
{
    qglviewer::Vec min_vec(-50, -50, -20);
    qglviewer::Vec max_vec(50, 50, 50);
    QGLViewer::setSceneBoundingBox(min_vec, max_vec);
}

void ViewerWidget::draw()
{
//    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);

    glEnable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
//    glEnable(GL_CULL_FACE);
//    glCullFace(GL_BACK);

//    camera()->fitBoundingBox(qglviewer::Vec(-10, -10, -10), qglviewer::Vec(10, 10, 10));
//    camera()->setSceneCenter(qglviewer::Vec(0, 0, 0));
    QMatrix4x4 pvm_matrix;
    camera()->getModelViewProjectionMatrix(pvm_matrix.data());
    QMatrix4x4 view_matrix;
    camera()->getModelViewMatrix(view_matrix.data());
    QMatrix4x4 model_matrix; // Identity
    model_matrix.setToIdentity();

    // draw drawable objects:
    octree_drawer_.draw(pvm_matrix, view_matrix, model_matrix);
    sparce_recon_drawer_.draw(pvm_matrix, width(), height());
    planned_viewpoint_drawer_.draw(pvm_matrix, width(), height());

    if (display_axes_) {
      axes_drawer_.draw(pvm_matrix, width(), height(), 5.0f);
    }
}

void ViewerWidget::drawWithNames()
{
}

void ViewerWidget::postDraw()
{
}

void ViewerWidget::postSelection(const QPoint&)
{
}

void ViewerWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        sparce_recon_drawer_.changePointSize(event->delta());
        event->accept();
        updateGL();
    } else if (event->modifiers() & Qt::AltModifier) {
        sparce_recon_drawer_.changeCameraSize(event->delta());
        event->accept();
        updateGL();
    } else if (event->modifiers() & Qt::ShiftModifier) {
//      ChangeNearPlane(event->delta());
        QGLViewer::wheelEvent(event);
    } else {
//      ChangeFocusDistance(event->delta());
        QGLViewer::wheelEvent(event);
    }
}

void ViewerWidget::onCameraPoseTimeoutHandlerFinished() {
  camera_pose_timer_->start(100);
}

void ViewerWidget::onCameraPoseTimeout() {
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  worker_thread_ = std::thread([this]() {
    ait::Pose camera_pose = getCameraPose();
//    CameraId camera_id = planner_->getReconstruction()->getCameras().cbegin()->first;
//    std::cout << "pose_matrix image to world: " << camera_pose.getTransformationImageToWorld() << std::endl;
//    std::cout << "translation: " << camera_pose.inverse().translation();
//    std::unordered_set<Point3DId> proj_points = planner_->computeProjectedMapPoints(camera_id, camera_pose);
//    std::unordered_set<Point3DId> filtered_points = planner_->computeFilteredMapPoints(camera_id, camera_pose);
//    std::unordered_set<Point3DId> visible_points = planner_->computeVisibleMapPoints(camera_id, camera_pose);
//
//    std::cout << "  projected points: " << proj_points.size() << std::endl;
//    std::cout << "  filtered points: " << filtered_points.size() << std::endl;
//    std::cout << "  non-occluded points: " << visible_points.size() << std::endl;
//    std::cout << "  filtered and non-occluded: " << ait::computeSetIntersectionSize(filtered_points, visible_points) << std::endl;

//    double information_score = planner_->computeInformationScore(camera_pose);
//    std::cout << "  information score: " << information_score << std::endl;
    double information_score_bvh = planner_->computeInformationScoreBVH(camera_pose);
    std::cout << "  information score BVH: " << information_score_bvh << std::endl;

    emit this->cameraPoseTimeoutHandlerFinished();
  });
}