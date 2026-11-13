// A Material confirmation dialog for actions that restart, erase or overwrite the device (spec
// §18.7). `const [confirm, dialog] = useConfirm()`: `await confirm({...})` resolves true only when
// the person presses the action button; Cancel, Escape and the backdrop resolve false. Render
// `dialog` once in the component's tree.
import { useCallback, useState, type ReactNode } from 'react';
import {
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogContentText,
  DialogTitle,
} from '@mui/material';

export interface ConfirmOptions {
  title: string;
  message: string;
  confirmLabel: string;
  // Destructive actions use the error colour, and Cancel keeps focus by default.
  destructive?: boolean;
}

interface Pending extends ConfirmOptions {
  resolve: (answer: boolean) => void;
}

export function useConfirm(): [
  (options: ConfirmOptions) => Promise<boolean>,
  ReactNode,
] {
  const [pending, setPending] = useState<Pending | null>(null);
  const confirm = useCallback(
    (options: ConfirmOptions) =>
      new Promise<boolean>((resolve) => setPending({ ...options, resolve })),
    [],
  );
  const answer = (value: boolean) => {
    pending?.resolve(value);
    setPending(null);
  };
  const dialog = (
    <Dialog
      open={pending !== null}
      onClose={() => answer(false)}
      aria-labelledby="confirm-title"
      aria-describedby="confirm-message"
    >
      <DialogTitle id="confirm-title">{pending?.title}</DialogTitle>
      <DialogContent>
        <DialogContentText id="confirm-message">
          {pending?.message}
        </DialogContentText>
      </DialogContent>
      <DialogActions>
        <Button onClick={() => answer(false)} autoFocus>
          Cancel
        </Button>
        <Button
          onClick={() => answer(true)}
          color={pending?.destructive ? 'error' : 'primary'}
          variant="contained"
        >
          {pending?.confirmLabel}
        </Button>
      </DialogActions>
    </Dialog>
  );
  return [confirm, dialog];
}
