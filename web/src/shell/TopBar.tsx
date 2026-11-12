// The top bar (spec §18.3): the page title, the device's network name, and icon buttons for the
// theme and for signing in or out. Under it, only the device conditions that need attention.
import type { ReactNode } from 'react';
import { Box, IconButton, Tooltip, Typography } from '@mui/material';
import { useTheme } from '@mui/material/styles';
import DarkModeOutlined from '@mui/icons-material/DarkModeOutlined';
import LightModeOutlined from '@mui/icons-material/LightModeOutlined';
import LoginOutlined from '@mui/icons-material/LoginOutlined';
import LogoutOutlined from '@mui/icons-material/LogoutOutlined';
import ScheduleOutlined from '@mui/icons-material/ScheduleOutlined';
import ScienceOutlined from '@mui/icons-material/ScienceOutlined';
import SdCardAlertOutlined from '@mui/icons-material/SdCardAlertOutlined';
import WifiOffOutlined from '@mui/icons-material/WifiOffOutlined';
import WifiOutlined from '@mui/icons-material/WifiOutlined';
import type { Snapshot } from '../api/generated';

export default function TopBar({
  title,
  hostname,
  dark,
  onToggleTheme,
  signedIn,
  onSignIn,
  onSignOut,
}: {
  title: string;
  hostname: string | null;
  dark: boolean;
  onToggleTheme: () => void;
  signedIn: boolean;
  onSignIn: () => void;
  onSignOut: () => void;
}) {
  const themeLabel = dark ? 'Switch to light theme' : 'Switch to dark theme';
  const accountLabel = signedIn ? 'Sign out' : 'Sign in';
  return (
    <header className="app-top-bar">
      <Typography variant="headlineMedium" component="h1">
        {title}
      </Typography>
      <div className="app-top-actions">
        {hostname && (
          <span className="app-network-name">
            <WifiOutlined aria-hidden="true" sx={{ fontSize: 18 }} />
            {`${hostname}.local`}
          </span>
        )}
        <Tooltip title={themeLabel}>
          <IconButton onClick={onToggleTheme} size="large">
            {dark ? <LightModeOutlined /> : <DarkModeOutlined />}
          </IconButton>
        </Tooltip>
        <Tooltip title={accountLabel}>
          <IconButton onClick={signedIn ? onSignOut : onSignIn} size="large">
            {signedIn ? <LogoutOutlined /> : <LoginOutlined />}
          </IconButton>
        </Tooltip>
      </div>
    </header>
  );
}

interface Condition {
  label: string;
  icon: ReactNode;
}

// Device conditions worth a glance; when everything is fine there is nothing to show.
function conditions(snapshot: Snapshot | null): Condition[] {
  if (!snapshot) return [];
  const list: Condition[] = [];
  const wifi = snapshot.wifi.state;
  if (wifi !== 'connected')
    list.push({
      label:
        wifi === 'setup'
          ? 'Setup access point open'
          : wifi === 'connecting'
            ? 'Wi-Fi connecting'
            : 'Wi-Fi offline',
      icon: <WifiOffOutlined />,
    });
  if (!snapshot.clock.synchronized)
    list.push({ label: 'Clock not synchronized', icon: <ScheduleOutlined /> });
  if (!snapshot.logging.healthy)
    list.push({
      label: 'History logging unavailable',
      icon: <SdCardAlertOutlined />,
    });
  if (snapshot.simulated)
    list.push({ label: 'Simulated readings', icon: <ScienceOutlined /> });
  return list;
}

export function DeviceConditions({ snapshot }: { snapshot: Snapshot | null }) {
  const theme = useTheme();
  const warning = theme.palette.status.warning;
  const list = conditions(snapshot);
  if (!list.length) return null;
  return (
    <Box
      component="ul"
      aria-label="Device conditions"
      sx={{ m: 0, mb: 3, p: 0, display: 'flex', flexWrap: 'wrap', gap: 1 }}
    >
      {list.map(({ label, icon }) => (
        <Box
          component="li"
          key={label}
          sx={{
            listStyle: 'none',
            display: 'flex',
            alignItems: 'center',
            gap: 1,
            height: 32,
            px: 1.5,
            borderRadius: `${theme.corner.small}px`,
            bgcolor: warning.container,
            color: warning.onContainer,
            ...theme.typography.labelLarge,
            '& svg': { fontSize: 18, color: warning.color },
          }}
        >
          <Box component="span" aria-hidden="true" sx={{ display: 'flex' }}>
            {icon}
          </Box>
          {label}
        </Box>
      ))}
    </Box>
  );
}
