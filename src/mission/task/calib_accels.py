import math
import numpy as np

from props import getNode, PropertyNode

import comms.events
from mission.task.task import Task
from mission.task.lowpass import LowPass

g = 9.81                        # gravity

# state key:
#   0 = right side up
#   1 = up side down
#   2 = nose down
#   3 = nose up
#   4 = right wing up
#   5 = right wing down
#   6 = sanity check
#   7 = complete ok
#   8 = complete failed

def affine_matrix_from_points(v0, v1, shear=True, scale=True, usesvd=True, usesparse=True):
    """Return affine transform matrix to register two point sets.

    v0 and v1 are shape (ndims, \*) arrays of at least ndims non-homogeneous
    coordinates, where ndims is the dimensionality of the coordinate space.

    If shear is False, a similarity transformation matrix is returned.
    If also scale is False, a rigid/Euclidean transformation matrix
    is returned.

    By default the algorithm by Hartley and Zissermann [15] is used.
    If usesvd is True, similarity and Euclidean transformation matrices
    are calculated by minimizing the weighted sum of squared deviations
    (RMSD) according to the algorithm by Kabsch [8].
    Otherwise, and if ndims is 3, the quaternion based algorithm by Horn [9]
    is used, which is slower when using this Python implementation.

    The returned matrix performs rotation, translation and uniform scaling
    (if specified).

    >>> v0 = [[0, 1031, 1031, 0], [0, 0, 1600, 1600]]
    >>> v1 = [[675, 826, 826, 677], [55, 52, 281, 277]]
    >>> affine_matrix_from_points(v0, v1)
    array([[   0.14549,    0.00062,  675.50008],
           [   0.00048,    0.14094,   53.24971],
           [   0.     ,    0.     ,    1.     ]])
    >>> T = translation_matrix(np.random.random(3)-0.5)
    >>> R = random_rotation_matrix(np.random.random(3))
    >>> S = scale_matrix(random.random())
    >>> M = concatenate_matrices(T, R, S)
    >>> v0 = (np.random.rand(4, 100) - 0.5) * 20
    >>> v0[3] = 1
    >>> v1 = np.dot(M, v0)
    >>> v0[:3] += np.random.normal(0, 1e-8, 300).reshape(3, -1)
    >>> M = affine_matrix_from_points(v0[:3], v1[:3])
    >>> np.allclose(v1, np.dot(M, v0))
    True

    More examples in superimposition_matrix()

    """
    v0 = np.array(v0, dtype=np.float64, copy=True)
    v1 = np.array(v1, dtype=np.float64, copy=True)

    # print( "v0.shape = %s" % str(v0.shape))
    # print( "v1.shape = %s" % str(v1.shape))
    # print( "v0.shape[1] = %s" % str(v0.shape[1]))
    ndims = v0.shape[0]
    if ndims < 2 or v0.shape[1] < ndims or v0.shape != v1.shape:
        raise ValueError("input arrays are of wrong shape or type")

    # move centroids to origin
    t0 = -np.mean(v0, axis=1)
    M0 = np.identity(ndims+1)
    M0[:ndims, ndims] = t0
    v0 += t0.reshape(ndims, 1)
    t1 = -np.mean(v1, axis=1)
    M1 = np.identity(ndims+1)
    M1[:ndims, ndims] = t1
    v1 += t1.reshape(ndims, 1)

    if shear:
        # Affine transformation
        A = np.concatenate((v0, v1), axis=0)
        if usesparse:
            u, s, vh = scipy.sparse.linalg.svds(A.T, k=3)
        else:
            u, s, vh = np.linalg.svd(A.T)
        vh = vh[:ndims].T
        B = vh[:ndims]
        C = vh[ndims:2*ndims]
        t = np.dot(C, np.linalg.pinv(B))
        t = np.concatenate((t, np.zeros((ndims, 1))), axis=1)
        M = np.vstack((t, ((0.0,)*ndims) + (1.0,)))
    elif usesvd or ndims != 3:
        # Rigid transformation via SVD of covariance matrix
        u, s, vh = np.linalg.svd(np.dot(v1, v0.T))
        # rotation matrix from SVD orthonormal bases
        R = np.dot(u, vh)
        if np.linalg.det(R) < 0.0:
            # R does not constitute right handed system
            R -= np.outer(u[:, ndims-1], vh[ndims-1, :]*2.0)
            s[-1] *= -1.0
        # homogeneous transformation matrix
        M = np.identity(ndims+1)
        M[:ndims, :ndims] = R
    else:
        # Rigid transformation matrix via quaternion
        # compute symmetric matrix N
        xx, yy, zz = np.sum(v0 * v1, axis=1)
        xy, yz, zx = np.sum(v0 * np.roll(v1, -1, axis=0), axis=1)
        xz, yx, zy = np.sum(v0 * np.roll(v1, -2, axis=0), axis=1)
        N = [[xx+yy+zz, 0.0,      0.0,      0.0],
             [yz-zy,    xx-yy-zz, 0.0,      0.0],
             [zx-xz,    xy+yx,    yy-xx-zz, 0.0],
             [xy-yx,    zx+xz,    yz+zy,    zz-xx-yy]]
        # quaternion: eigenvector corresponding to most positive eigenvalue
        w, V = np.linalg.eigh(N)
        q = V[:, np.argmax(w)]
        q /= vector_norm(q)  # unit quaternion
        # homogeneous transformation matrix
        M = quaternion_matrix(q)

    if scale and not shear:
        # Affine transformation; scale is ratio of RMS deviations from centroid
        v0 *= v0
        v1 *= v1
        M[:ndims, :ndims] *= math.sqrt(np.sum(v1) / np.sum(v0))

    # move centroids back
    M = np.dot(np.linalg.inv(M1), np.dot(M, M0))
    M /= M[ndims, ndims]
    return M

class CalibrateAccels(Task):
    def __init__(self, config_node):
        Task.__init__(self)
        self.imu_node = getNode("/sensors/imu", True)
        self.config_imu_node = getNode("/config/drivers/Aura4/imu")
        self.state = 0
        self.ax_slow = LowPass(time_factor=2.0) 
        self.ax_fast = LowPass(time_factor=0.2) 
        self.ay_slow = LowPass(time_factor=2.0) 
        self.ay_fast = LowPass(time_factor=0.2) 
        self.az_slow = LowPass(time_factor=2.0) 
        self.az_fast = LowPass(time_factor=0.2)
        self.armed = False
        self.ref = [ [  0,  0, -g ],
                     [  0,  0,  g ],
                     [ -g,  0,  0 ],
                     [  g,  0,  0 ],
                     [  0, -g,  0 ],
                     [  0,  g,  0 ] ]
        self.meas = list(self.ref) # copy
        self.checked = {}
        
    def activate(self):
        self.active = True
        self.armed = False
        self.checked = {}
        comms.events.log("calibrate accels", "active")

    def detect_up(self):
        ax = self.ax_fast.filter_value
        ay = self.ay_fast.filter_value
        az = self.az_fast.filter_value
        if ax > 8: return "x-pos"    # nose up
        elif ax < -8: return "x-neg" # nose down
        if ay > 8: return "y-pos"    # right wing down
        elif ay < -8: return "y-neg" # right wing up
        if az > 8: return "z-pos"    # up side down
        elif az < -8: return "z-neg" # right side up
        return "none"                # no dominate axis up

    def new_axis(self):
        up_axis = self.detect_up()
        if up_axis == "none":
            return False
        elif up_axis in self.checked:
            return False
        else:
            return True
        
    def update(self, dt):
        if not self.active:
            return False

        # update filters
        ax = self.imu_node.getFloat("ax_nocal")
        ay = self.imu_node.getFloat("ay_nocal")
        az = self.imu_node.getFloat("az_nocal")
        self.ax_slow.update(ax, dt)
        self.ax_fast.update(ax, dt)
        self.ay_slow.update(ay, dt)
        self.ay_fast.update(ay, dt)
        self.az_slow.update(az, dt)
        self.az_fast.update(az, dt)

        # (no) motion test
        ax_diff = self.ax_slow.filter_value - self.ax_fast.filter_value
        ay_diff = self.ay_slow.filter_value - self.ay_fast.filter_value
        az_diff = self.az_slow.filter_value - self.az_fast.filter_value
        d = math.sqrt(ax_diff*ax_diff + ay_diff*ay_diff + az_diff*az_diff)
        if d < 0.04:
            stable = True
        else:
            stable = False
            
        up_axis = self.detect_up()
        if up_axis == "none":
            self.armed = True
        if self.state < 6:
            print("up axis:", up_axis, "armed:", self.armed, " slow-fast: %.3f" % d, " stable:", stable)
              
        if self.state == 0:
            print("Place level and right side up - stable:", stable)
            if self.armed and stable and self.new_axis():
                self.meas[self.state] = [ self.ax_fast.filter_value,
                                          self.ay_fast.filter_value,
                                          self.az_fast.filter_value ]
                self.checked[up_axis] = True
                self.state += 1
                self.armed = False
        elif self.state == 1:
            print("Place up side down - stable:", stable)
            if self.armed and stable and self.new_axis():
                self.meas[self.state] = [ self.ax_fast.filter_value,
                                          self.ay_fast.filter_value,
                                          self.az_fast.filter_value ]
                self.checked[up_axis] = True
                self.state += 1
                self.armed = False
        elif self.state == 2:
            print("Place nose down - stable:", stable)
            if self.armed and stable and self.new_axis():
                self.meas[self.state] = [ self.ax_fast.filter_value,
                                          self.ay_fast.filter_value,
                                          self.az_fast.filter_value ]
                self.checked[up_axis] = True
                self.state += 1
                self.armed = False
        elif self.state == 3:
            print("Place nose up - stable:", stable)
            if self.armed and stable and self.new_axis():
                self.meas[self.state] = [ self.ax_fast.filter_value,
                                          self.ay_fast.filter_value,
                                          self.az_fast.filter_value ]
                self.checked[up_axis] = True
                self.state += 1
                self.armed = False
        elif self.state == 4:
            print("Place right wing down - stable:", stable)
            if self.armed and stable and self.new_axis():
                self.meas[self.state] = [ self.ax_fast.filter_value,
                                          self.ay_fast.filter_value,
                                          self.az_fast.filter_value ]
                self.checked[up_axis] = True
                self.state += 1
                self.armed = False
        elif self.state == 5:
            print("Place right wing up - stable:", stable)
            if self.armed and stable and self.new_axis():
                self.meas[self.state] = [ self.ax_fast.filter_value,
                                          self.ay_fast.filter_value,
                                          self.az_fast.filter_value ]
                self.checked[up_axis] = True
                self.state += 1
                self.armed = False
        elif self.state == 6:
            # did we measure 6 unique axes?
            if len(self.checked) != 6:
                print("Somehow didn't calibrate 6 orientations. :-(")
                self.state += 2
            else:
                # compute affine rotation fit
                v0 = np.array(self.meas, dtype=np.float64, copy=True).T
                v1 = np.array(self.ref, dtype=np.float64, copy=True).T
                M = affine_matrix_from_points(v0, v1, shear=False, scale=False)
                R = M[:3,:3]
                print(R @ R.T)
                print(R)
                # R should be orthogonal/normalized here
                # check if any row column doesn't have an element close to 1
                if np.max(np.abs(R[0])) < 0.9:
                    print("bad row 1")
                    self.state += 2
                elif np.max(np.abs(R[1])) < 0.9:
                    print("bad row 2")
                    self.state += 2
                elif np.max(np.abs(R[2])) < 0.9:
                    print("bad row 3")
                    self.state += 2
                elif np.max(np.abs(R[:,0])) < 0.9:
                    print("bad column 1")
                    self.state += 2
                elif np.max(np.abs(R[:,1])) < 0.9:
                    print("bad column 2")
                    self.state += 2
                elif np.max(np.abs(R[:,2])) < 0.9:
                    print("bad column 3")
                    self.state += 2
                else:
                    # nothing bad detected, save results and goto success state
                    self.R = R
                    self.T = M[:,:4][:3] # 1st 3 elements of the 4th column
                    self.state += 1
        elif self.state == 7:
            # calibration complete, success, report!
            print("calibration succeeded")
            # consider current orientation matrix if it exists
            if self.config_imu_node and self.config_imu_node.hasChild("orientation"):
                current = []
                for i in range(9):
                    current.append(self.config_imu_node.getFloatEnum("orientation", i))
                current = np.array(current).reshape(3,3)
            else:
                current = np.eye(3)
            print("current:")
            print(current)
            print("R:")
            print(self.R)
            final = current @ self.R
            print("Final:")
            print(final)
            # as if this wasn't already fancy enough, get even fancier!
            errors = []
            for i, v in enumerate(self.meas):
                v1 = v @ final
                v0 = self.ref[i]
                err = np.linalg.norm(v0 - v1)
                errors.append(err)
            print("errors:", errors)
            mean = np.mean(errors)
            std = np.std(errors)
            print("calibration mean:", mean, " std:", std)
            self.state += 2
            calib_node = PropertyNode()
            calib_node.setLen("orientation", 9)
            for i in range(9):
                calib_node.setFloatEnum("orientation", i, final.flatten()[i])
            calib_node.setFloat("calibration_mean", mean)
            calib_node.setFloat("calibration_std", std)
            logging_node = getNode("/config/logging", true)
            dir = logging_node.getString("flight_dir")
            props_json.save(os.path.join(dir, "imu_calib.json"), calib_node)
        elif self.state == 8:
            # calibration complete, but failed. :-(
            print("calibration failed")
            pass            

    def is_complete(self):
        return False

    def close(self):
        self.active = False
        return True
