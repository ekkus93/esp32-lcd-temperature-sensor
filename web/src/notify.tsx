// Completed-save notices (spec §18.7): a snackbar at the bottom of the screen says a change was
// saved. Progress and errors stay inline, next to what they concern.
import { useCallback, useState, type ReactNode } from 'react';
import { Snackbar } from '@mui/material';
import { NoticeContext, type Notify } from './noticeContext';

export function NoticeProvider({ children }: { children: ReactNode }) {
  const [notice, setNotice] = useState<{ id: number; text: string } | null>(
    null,
  );
  const notify = useCallback<Notify>(
    (text) => setNotice({ id: Date.now(), text }),
    [],
  );
  return (
    <NoticeContext.Provider value={notify}>
      {children}
      <Snackbar
        key={notice?.id}
        open={notice !== null}
        message={notice?.text ?? ''}
        autoHideDuration={6000}
        anchorOrigin={{ vertical: 'bottom', horizontal: 'center' }}
        // Above the phone's bottom navigation bar.
        sx={{ bottom: { xs: 96, sm: 24 } }}
        onClose={(_, reason) => {
          if (reason !== 'clickaway') setNotice(null);
        }}
      />
    </NoticeContext.Provider>
  );
}
