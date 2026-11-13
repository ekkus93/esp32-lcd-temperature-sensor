// A loading placeholder (spec §18.8): a skeleton of the content's shape, with the words of the
// loading message kept for screen readers.
import { Box, Skeleton } from '@mui/material';

export default function LoadingBlock({
  label,
  height = 160,
}: {
  label: string;
  height?: number;
}) {
  return (
    <Box role="status" aria-live="polite">
      <span className="visually-hidden">{label}</span>
      <Skeleton
        variant="rounded"
        height={height}
        sx={{ borderRadius: '16px' }}
        aria-hidden="true"
      />
    </Box>
  );
}
