// The notice channel shared by the provider and the views that send notices (spec §18.7).
import { createContext, useContext } from 'react';

export type Notify = (text: string) => void;
export const NoticeContext = createContext<Notify>(() => undefined);

// Sends a completed-save notice; without a provider, nothing is shown.
export function useNotify(): Notify {
  return useContext(NoticeContext);
}
