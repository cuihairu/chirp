import React, { useState, useEffect } from 'react';
import {
  Box, Typography, Table, TableBody, TableCell, TableContainer,
  TableHead, TableRow, Paper, Chip, IconButton, Button
} from '@mui/material';
import {
  Delete as DeleteIcon,
  Edit as EditIcon,
  Add as AddIcon
} from '@mui/icons-material';

interface Channel {
  id: string;
  name: string;
  type: 'text' | 'voice' | 'announcement';
  category: string;
  memberCount: number;
  messageCount: number;
  createdAt: string;
}

const Channels: React.FC = () => {
  const [channels, setChannels] = useState<Channel[]>([]);

  useEffect(() => {
    // Fetch channels from API
    fetchChannels();
  }, []);

  const fetchChannels = async () => {
    try {
      const response = await fetch('/api/channels');
      if (response.ok) {
        const data = await response.json();
        setChannels(data);
      }
    } catch (error) {
      console.error('Failed to fetch channels:', error);
      // Use mock data
      setChannels([
        { id: 'ch_1', name: 'general', type: 'text', category: 'General', memberCount: 1523, messageCount: 45678, createdAt: '2024-01-01' },
        { id: 'ch_2', name: 'voice-general', type: 'voice', category: 'Voice', memberCount: 45, messageCount: 0, createdAt: '2024-01-01' },
        { id: 'ch_3', name: 'announcements', type: 'announcement', category: 'Info', memberCount: 2341, messageCount: 234, createdAt: '2024-01-01' },
        { id: 'ch_4', name: 'off-topic', type: 'text', category: 'General', memberCount: 892, messageCount: 12345, createdAt: '2024-01-15' },
        { id: 'ch_5', name: 'gaming', type: 'text', category: 'Topics', memberCount: 567, messageCount: 8901, createdAt: '2024-02-01' },
      ]);
    }
  };

  const getTypeColor = (type: string) => {
    switch (type) {
      case 'text': return 'primary';
      case 'voice': return 'secondary';
      case 'announcement': return 'warning';
      default: return 'default';
    }
  };

  return (
    <Box>
      <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', mb: 3 }}>
        <Typography variant="h4">Channels</Typography>
        <Button variant="contained" startIcon={<AddIcon />}>
          Create Channel
        </Button>
      </Box>

      <TableContainer component={Paper}>
        <Table>
          <TableHead>
            <TableRow>
              <TableCell>Name</TableCell>
              <TableCell>Type</TableCell>
              <TableCell>Category</TableCell>
              <TableCell align="right">Members</TableCell>
              <TableCell align="right">Messages</TableCell>
              <TableCell>Created</TableCell>
              <TableCell align="right">Actions</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {channels.map((channel) => (
              <TableRow key={channel.id} hover>
                <TableCell>
                  <Typography variant="body2" fontWeight="medium">
                    #{channel.name}
                  </Typography>
                </TableCell>
                <TableCell>
                  <Chip
                    label={channel.type}
                    color={getTypeColor(channel.type) as any}
                    size="small"
                  />
                </TableCell>
                <TableCell>{channel.category}</TableCell>
                <TableCell align="right">{channel.memberCount.toLocaleString()}</TableCell>
                <TableCell align="right">{channel.messageCount.toLocaleString()}</TableCell>
                <TableCell>{channel.createdAt}</TableCell>
                <TableCell align="right">
                  <IconButton size="small">
                    <EditIcon fontSize="small" />
                  </IconButton>
                  <IconButton size="small" color="error">
                    <DeleteIcon fontSize="small" />
                  </IconButton>
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </TableContainer>
    </Box>
  );
};

export default Channels;
