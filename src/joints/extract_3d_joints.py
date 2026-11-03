"""
Minimal RaBit joint extraction: no RaBitModel, no faces, no skinning weights.

Only these files from <dataroot>/shape/ are read:
    mean.obj, pcamat.npy, clusterdic.npy, joint2index.npy,
    pose_order.npy, ktree_table.npy

Usage:
    python rabit_joints.py beta.npy pose.npy [trans.npy] [--out joints.txt]

--out format is chosen by extension: .txt (for the C++ viewer), .json, .npy, .obj
"""
import argparse
import json

import numpy as np


def _load_obj_vertices(path):
    with open(path) as f:
        return np.array(
            [list(map(float, l.split()[1:4])) for l in f if l.startswith("v ")]
        )


def _rodrigues(r):
    """r: (N, 3) axis-angle -> (N, 3, 3) rotation matrices."""
    theta = np.linalg.norm(r, axis=1, keepdims=True)
    axis = r / np.maximum(theta, 1e-12)
    x, y, z = axis[:, 0], axis[:, 1], axis[:, 2]
    zero = np.zeros_like(x)
    K = np.stack([zero, -z, y, z, zero, -x, -y, x, zero], axis=1).reshape(-1, 3, 3)
    t = theta.reshape(-1, 1, 1)
    return np.eye(3) + np.sin(t) * K + (1 - np.cos(t)) * (K @ K)


def get_joints(beta, pose, trans=None, dataroot="./rabit_data/", return_parents=False):
    """
    beta : (<=500,) shape coefficients
    pose : (24, 3) axis-angle in dataset order (row 0 = root, ignored like in
           RaBitModel.set_params). A (23, 3) pose is taken as already in the
           model's internal order.
    trans: (3,) global translation (optional)

    Returns (names, posed_joints (24, 3), rest_joints (24, 3)),
    plus parents (24,) as a 4th value if return_parents=True.
    """
    s = dataroot + "shape/"
    pcamat = np.load(s + "pcamat.npy", allow_pickle=True)           # (500, 3V)
    clusterdic = np.load(s + "clusterdic.npy", allow_pickle=True).item()
    joint2index = np.load(s + "joint2index.npy", allow_pickle=True).item()
    joint_order = np.load(s + "pose_order.npy")
    ktree = np.load(s + "ktree_table.npy", allow_pickle=True).item()
    v_template = _load_obj_vertices(s + "mean.obj")                 # (V, 3)

    index2cluster = {v: k for k, v in clusterdic.items()}
    names = [index2cluster[i] for i in range(len(index2cluster))]
    n = 24

    # kinematic tree and joint-order mapping
    parent = np.full(n, -1, dtype=int)
    name2index = {}
    for i in range(1, n):
        parent[i] = int(ktree[i][1])
        name2index[ktree[i][0]] = i
    reorder = np.zeros(n, dtype=int)
    for i, jname in enumerate(joint_order):
        if jname in name2index:
            reorder[name2index[jname]] = i
        else:
            reorder[0] = 2

    # pose -> (23, 3)
    pose = np.asarray(pose, dtype=np.float64).reshape(-1, 3)
    if pose.shape[0] == 24:
        pose = pose[reorder][1:]
    elif pose.shape[0] != 23:
        raise ValueError(f"pose must have 24 or 23 rows, got {pose.shape[0]}")

    # shaped rest mesh (beta may be shorter than 500)
    beta = np.asarray(beta, dtype=np.float64).reshape(-1)
    k = min(len(beta), pcamat.shape[0])
    v_shaped = (pcamat[:k].T @ beta[:k] + v_template.reshape(-1)).reshape(-1, 3)

    # rest joints: midpoint of each cluster's bounding box
    J = np.zeros((n, 3))
    for i, key in enumerate(names):
        if key == "RootNode":
            continue
        pts = v_shaped[joint2index[key]]
        J[i] = (pts.max(axis=0) + pts.min(axis=0)) / 2

    # forward kinematics (root rotation is identity, as in RaBitModel)
    R = _rodrigues(np.vstack([np.zeros((1, 3)), pose]))
    G = np.empty((n, 4, 4))
    G[0] = np.eye(4)
    G[0, :3, 3] = J[0]
    for i in range(1, n):
        local = np.eye(4)
        local[:3, :3] = R[i]
        local[:3, 3] = J[i] - J[parent[i]]
        G[i] = G[parent[i]] @ local

    posed = G[:, :3, 3]
    if trans is not None:
        posed = posed + np.asarray(trans, dtype=np.float64).reshape(1, 3)
    if return_parents:
        return names, posed, J, parent
    return names, posed, J


def export_joints(path, names, joints, parents):
    """
    Save joints by file extension:
      .txt  : one line per joint "index parent x y z name" (read by view_joints.cpp)
      .json : {"names", "parents", "joints"}
      .npy  : (24, 3) array
      .obj  : points as "v x y z" plus bones as "l a b" (1-based)
    """
    joints = np.asarray(joints)
    ext = path.lower().rsplit(".", 1)[-1]
    if ext == "txt":
        with open(path, "w") as f:
            f.write("# index parent x y z name\n")
            for i, (nm, p, q) in enumerate(zip(names, joints, parents)):
                f.write(f"{i} {int(q)} {p[0]:.6f} {p[1]:.6f} {p[2]:.6f} {nm}\n")
    elif ext == "json":
        with open(path, "w") as f:
            json.dump({"names": list(names),
                       "parents": [int(q) for q in parents],
                       "joints": joints.tolist()}, f, indent=2)
    elif ext == "npy":
        np.save(path, joints)
    elif ext == "obj":
        with open(path, "w") as f:
            for p in joints:
                f.write(f"v {p[0]} {p[1]} {p[2]}\n")
            for i, q in enumerate(parents):
                if q >= 0:
                    f.write(f"l {int(q) + 1} {i + 1}\n")
    else:
        raise ValueError(f"unsupported extension: .{ext}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("source_folder")
    # ap.add_argument("rabit_data_folder")
    # ap.add_argument("beta")
    # ap.add_argument("pose")
    # ap.add_argument("trans", nargs="?")
    # ap.add_argument("--out", help="export path (.txt, .json, .npy, .obj)")
    # ap.add_argument("--dataroot", default="./rabit_data/")
    a = ap.parse_args()
    dataroot = "assets/RaBit/"

    beta = np.load(dataroot+ a.source_folder + "/3d_model/beta.npy", allow_pickle=True)
    pose = np.load(dataroot + a.source_folder + "/3d_model/pose.npy", allow_pickle=True)
    # trans = np.load(dataroot + a.source_folder + "/3d_model/trans.npy", allow_pickle=True) if a.trans else None

    names, joints, _, parents = get_joints(beta, pose, None, dataroot + "rabit_data/", return_parents=True)
    for i, (nm, p) in enumerate(zip(names, joints)):
        print(f"{i:2d} {nm:<20s} {p[0]: .5f} {p[1]: .5f} {p[2]: .5f}")
    export_joints(dataroot + a.source_folder + "/3d_model/3d_joints.txt", names, joints, parents)
    print("Saved:", dataroot + a.source_folder + "/3d_model/3d_joints")
