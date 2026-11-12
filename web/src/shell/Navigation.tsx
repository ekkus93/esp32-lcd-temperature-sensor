// The main navigation (spec §18.3): a rail with the app's mark at 840 px and wider, a bar at the
// bottom below that (styles.css). Destinations are real links, so they open in a new tab too; a
// plain click switches page in place.
import type { MouseEvent, ReactNode } from 'react';
import { Badge, SvgIcon } from '@mui/material';

export interface Destination {
  label: string;
  href: string;
  icon: ReactNode;
}

export function AppMark() {
  return (
    <span className="app-mark" aria-hidden="true">
      <SvgIcon sx={{ fontSize: 26 }}>
        <path
          d="M5 19c0-8 6-14 15-14 0 9-6 15-14 15M5 19l8-8"
          fill="none"
          stroke="currentColor"
          strokeWidth="1.8"
          strokeLinecap="round"
          strokeLinejoin="round"
        />
      </SvgIcon>
    </span>
  );
}

export default function Navigation({
  destinations,
  current,
  onNavigate,
  badges,
}: {
  destinations: Destination[];
  current: number;
  onNavigate: (index: number) => void;
  // A count shown on a destination's icon, by index (the active alerts on Alerts).
  badges: Record<number, number>;
}) {
  const follow = (event: MouseEvent<HTMLAnchorElement>, index: number) => {
    // Let the browser handle opening in a new tab or window.
    if (
      event.button !== 0 ||
      event.metaKey ||
      event.ctrlKey ||
      event.shiftKey ||
      event.altKey
    )
      return;
    event.preventDefault();
    onNavigate(index);
  };
  return (
    <nav aria-label="Main navigation" className="app-nav">
      <AppMark />
      <ul className="app-nav-list">
        {destinations.map((destination, i) => {
          const count = badges[i] ?? 0;
          return (
            <li key={destination.href}>
              <a
                href={destination.href}
                className="app-nav-item"
                aria-current={i === current ? 'page' : undefined}
                onClick={(event) => follow(event, i)}
              >
                <span className="app-nav-indicator">
                  <Badge
                    badgeContent={count > 0 ? count : null}
                    color="error"
                    max={9}
                    slotProps={{ badge: { 'aria-hidden': true } }}
                  >
                    {destination.icon}
                  </Badge>
                </span>
                <span className="app-nav-label">{destination.label}</span>
                {count > 0 && (
                  <span className="visually-hidden">, {count} active</span>
                )}
              </a>
            </li>
          );
        })}
      </ul>
    </nav>
  );
}
