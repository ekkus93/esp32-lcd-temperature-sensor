import { createContext, useContext } from 'react';
import type { AuthState } from './api/auth';
export type { AuthState };

// Readings, history and alerts are public. Only changing settings needs the owner's password, so
// the sign-in state is shared (the app bar and every owner page read it) instead of gating the
// whole app.
export interface AuthValue {
  state: AuthState | null;
  error: string;
  checking: boolean;
  busy: boolean;
  refresh: () => Promise<void>;
  signIn: (password: string, ownerCode?: string) => Promise<void>;
  signOut: () => Promise<void>;
}
export const AuthContext = createContext<AuthValue | null>(null);

export function useAuth(): AuthValue {
  const value = useContext(AuthContext);
  if (!value) throw new Error('useAuth needs an AuthProvider');
  return value;
}
