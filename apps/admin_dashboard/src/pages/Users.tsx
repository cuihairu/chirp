import React, { useState, useEffect } from 'react';
import {
  Box, Typography, Table, TableBody, TableCell, TableContainer,
  TableHead, TableRow, Paper, TextField, InputAdornment,
  Chip, IconButton, Menu, MenuItem
} from '@mui/material';
import {
  Search as SearchIcon,
  MoreVert as MoreVertIcon,
  Block as BlockIcon,
  CheckCircle as CheckCircleIcon
} from '@mui/icons-material';

interface User {
  id: string;
  username: string;
  email: string;
  status: 'online' | 'offline' | 'away';
  role: 'admin' | 'moderator' | 'user';
  joinedAt: string;
  lastSeen: string;
  messagesSent: number;
}

const Users: React.FC = () => {
  const [users, setUsers] = useState<User[]>([]);
  const [loading, setLoading] = useState(true);
  const [searchQuery, setSearchQuery] = useState('');
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const [selectedUser, setSelectedUser] = useState<User | null>(null);

  useEffect(() => {
    // Fetch users from API
    fetchUsers();
  }, []);

  const fetchUsers = async () => {
    try {
      const response = await fetch('/api/users');
      if (response.ok) {
        const data = await response.json();
        setUsers(data);
      }
    } catch (error) {
      console.error('Failed to fetch users:', error);
      // Use mock data for demo
      setUsers(generateMockUsers(50));
    } finally {
      setLoading(false);
    }
  };

  const generateMockUsers = (count: number): User[] => {
    const mockUsers: User[] = [];
    const statuses: Array<'online' | 'offline' | 'away'> = ['online', 'offline', 'away'];
    const roles: Array<'admin' | 'moderator' | 'user'> = ['admin', 'moderator', 'user'];

    for (let i = 1; i <= count; i++) {
      mockUsers.push({
        id: `user_${i}`,
        username: `User${i}`,
        email: `user${i}@example.com`,
        status: statuses[Math.floor(Math.random() * statuses.length)],
        role: roles[Math.floor(Math.random() * roles.length)],
        joinedAt: new Date(Date.now() - Math.random() * 365 * 24 * 3600 * 1000).toISOString(),
        lastSeen: new Date(Date.now() - Math.random() * 7 * 24 * 3600 * 1000).toISOString(),
        messagesSent: Math.floor(Math.random() * 10000),
      });
    }
    return mockUsers;
  };

  const handleMenuOpen = (event: React.MouseEvent<HTMLElement>, user: User) => {
    setAnchorEl(event.currentTarget);
    setSelectedUser(user);
  };

  const handleMenuClose = () => {
    setAnchorEl(null);
    setSelectedUser(null);
  };

  const handleAction = (action: string) => {
    if (!selectedUser) return;

    switch (action) {
      case 'kick':
        console.log('Kick user:', selectedUser.id);
        break;
      case 'ban':
        console.log('Ban user:', selectedUser.id);
        break;
      case 'promote':
        console.log('Promote user:', selectedUser.id);
        break;
      case 'demote':
        console.log('Demote user:', selectedUser.id);
        break;
    }
    handleMenuClose();
  };

  const filteredUsers = users.filter(user =>
    user.username.toLowerCase().includes(searchQuery.toLowerCase()) ||
    user.email.toLowerCase().includes(searchQuery.toLowerCase())
  );

  const getStatusColor = (status: string) => {
    switch (status) {
      case 'online': return 'success';
      case 'away': return 'warning';
      case 'offline': return 'default';
      default: return 'default';
    }
  };

  const getRoleColor = (role: string) => {
    switch (role) {
      case 'admin': return 'error';
      case 'moderator': return 'warning';
      default: return 'default';
    }
  };

  return (
    <Box>
      <Typography variant="h4" gutterBottom>
        Users
      </Typography>

      <TextField
        fullWidth
        placeholder="Search users..."
        value={searchQuery}
        onChange={(e) => setSearchQuery(e.target.value)}
        InputProps={{
          startAdornment: (
            <InputAdornment position="start">
              <SearchIcon />
            </InputAdornment>
          ),
        }}
        sx={{ mb: 3 }}
      />

      <TableContainer component={Paper}>
        <Table>
          <TableHead>
            <TableRow>
              <TableCell>User</TableCell>
              <TableCell>Status</TableCell>
              <TableCell>Role</TableCell>
              <TableCell>Messages</TableCell>
              <TableCell>Last Seen</TableCell>
              <TableCell align="right">Actions</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {filteredUsers.map((user) => (
              <TableRow key={user.id} hover>
                <TableCell>
                  <Box>
                    <Typography variant="body2" fontWeight="medium">
                      {user.username}
                    </Typography>
                    <Typography variant="caption" color="textSecondary">
                      {user.email}
                    </Typography>
                  </Box>
                </TableCell>
                <TableCell>
                  <Chip
                    label={user.status}
                    color={getStatusColor(user.status) as any}
                    size="small"
                  />
                </TableCell>
                <TableCell>
                  <Chip
                    label={user.role}
                    color={getRoleColor(user.role) as any}
                    size="small"
                    variant={user.role === 'user' ? 'outlined' : 'filled'}
                  />
                </TableCell>
                <TableCell>{user.messagesSent.toLocaleString()}</TableCell>
                <TableCell>
                  {new Date(user.lastSeen).toLocaleDateString()}
                </TableCell>
                <TableCell align="right">
                  <IconButton onClick={(e) => handleMenuOpen(e, user)}>
                    <MoreVertIcon />
                  </IconButton>
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </TableContainer>

      <Menu
        anchorEl={anchorEl}
        open={Boolean(anchorEl)}
        onClose={handleMenuClose}
      >
        <MenuItem onClick={() => handleAction('promote')}>
          Promote to Moderator
        </MenuItem>
        <MenuItem onClick={() => handleAction('demote')}>
          Demote to User
        </MenuItem>
        <MenuItem onClick={() => handleAction('kick')}>
          Kick from Server
        </MenuItem>
        <MenuItem onClick={() => handleAction('ban')} sx={{ color: 'error.main' }}>
          Ban User
        </MenuItem>
      </Menu>
    </Box>
  );
};

export default Users;
