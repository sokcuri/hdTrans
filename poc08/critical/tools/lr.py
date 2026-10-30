"""Sparse multinomial logistic regression (L1 via proximal gradient + small L2), numpy only."""
import numpy as np
class SparseLR:
    def __init__(s, l1=0.01, l2=1e-4, iters=400, lr=0.5):
        s.l1, s.l2, s.iters, s.lr = l1, l2, iters, lr
    def fit(s, X, y, feats, classes):
        s.fi = {f: i for i, f in enumerate(feats)}; s.cls = list(classes); ci = {c: i for i, c in enumerate(s.cls)}
        n, d, k = len(X), len(feats), len(s.cls)
        M = np.zeros((n, d))
        for r, fs in enumerate(X):
            for f in fs:
                if f in s.fi: M[r, s.fi[f]] = 1.0
        Y = np.zeros((n, k)); 
        for r, c in enumerate(y): Y[r, ci[c]] = 1
        W = np.zeros((d, k)); b = np.log(Y.mean(0) + 1e-3)
        for it in range(s.iters):
            Z = M @ W + b; Z -= Z.max(1, keepdims=True); P = np.exp(Z); P /= P.sum(1, keepdims=True)
            G = M.T @ (P - Y) / n + s.l2 * W; gb = (P - Y).mean(0)
            W -= s.lr * G; b -= s.lr * gb
            W = np.sign(W) * np.maximum(np.abs(W) - s.lr * s.l1, 0)
        s.W, s.b = W, b; return s
    def predict(s, X, allowed=None):
        out = []
        for fs in X:
            z = s.b.copy()
            for f in fs:
                i = s.fi.get(f)
                if i is not None: z = z + s.W[i]
            if allowed is not None:
                al = allowed(fs)
                z = np.array([z[j] if s.cls[j] in al else -1e9 for j in range(len(s.cls))])
            out.append(s.cls[int(np.argmax(z))])
        return out
    def top(s, n=15):
        inv = {i: f for f, i in s.fi.items()}; res = []
        for j, c in enumerate(s.cls):
            idx = np.argsort(-s.W[:, j])[:n]
            res.append((c, [(inv[i], round(float(s.W[i, j]), 2)) for i in idx if s.W[i, j] > 0]))
        return res
    def nnz(s): return int((np.abs(s.W) > 1e-9).sum())
