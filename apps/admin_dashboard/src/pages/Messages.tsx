import React, { useState, useEffect } from 'react';
import {
  Box, Typography, TextField, Button, Paper, List, ListItem,
  ListItemText, ListItemAvatar, Avatar, Chip, InputAdornment
} from '@mui/material';
import {
  Search as SearchIcon,
  Send as SendIcon
} from '@mui/icons-material';

interface Message {
  id: string;
  content: string;
  author: string;
  authorAvatar?: string;
  channel: string;
  timestamp: string;
  reactions: number;
}

const Messages: React.FC = () => {
  const [messages, setMessages] = useState<Message[]>([]);
  const [searchQuery, setSearchQuery] = useState('');

  useEffect(() => {
    fetchMessages();
  }, []);

  const fetchMessages = async () => {
    try {
      const response = await fetch('/api/messages?limit=50');
      if (response.ok) {
        const data = await response.json();
        setMessages(data);
      }
    } catch (error) {
      console.error('Failed to fetch messages:', error);
      // Use mock data
      setMessages([
        { id: '1', content: 'Hello everyone!', author: 'User1', channel: 'general', timestamp: '2024-03-18 10:30', reactions: 5 },
        { id: '2', content: 'Welcome to the server!', author: 'Admin', channel: 'general', timestamp: '2024-03-18 10:31', reactions: 12 },
        { id: '3', content: 'This is a test message', author: 'User2', channel: 'general', timestamp: '2024-03-18 10:32', reactions: 0 },
        { id: '4', content: 'Check out this cool feature!', author: 'User3', channel: 'off-topic', timestamp: '2024-03-18 10:33', reactions: 3 },
        { id: '5', content: 'Who wants to play some games?', author: 'User4', channel: 'gaming', timestamp: '2024-03-18 10:34', reactions: 8 },
      ]);
    }
  };

  const handleSearch = async () => {
    try {
      const response = await fetch(`/api/messages/search?q=${encodeURIComponent(searchQuery)}`);
      if (response.ok) {
        const data = await response.json();
        setMessages(data.results || []);
      }
    } catch (error) {
      console.error('Search failed:', error);
    }
  };

  return (
    <Box>
      <Typography variant="h4" gutterBottom>
        Messages
      </Typography>

      <Box sx={{ display: 'flex', gap: 2, mb: 3 }}>
        <TextField
          fullWidth
          placeholder="Search messages..."
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          onKeyPress={(e) => e.key === 'Enter' && handleSearch()}
          InputProps={{
            startAdornment: (
              <InputAdornment position="start">
                <SearchIcon />
              </InputAdornment>
            ),
          }}
        />
        <Button variant="contained" onClick={handleSearch} endIcon={<SendIcon />}>
          Search
        </Button>
      </Box>

      <Typography variant="subtitle2" color="textSecondary" gutterBottom>
        Recent messages
      </Typography>

      <Paper>
        <List>
          {messages.map((message) => (
            <ListItem key={message.id} alignItems="flex-start" divider>
              <ListItemAvatar>
                <Avatar>{message.author[0]}</Avatar>
              </ListItemAvatar>
              <ListItemText
                primary={
                  <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                    <Typography variant="subtitle2" fontWeight="medium">
                      {message.author}
                    </Typography>
                    <Chip label={message.channel} size="small" variant="outlined" />
                    <Typography variant="caption" color="textSecondary">
                      {message.timestamp}
                    </Typography>
                  </Box>
                }
                secondary={
                  <Box>
                    <Typography variant="body2" sx={{ mt: 1 }}>
                      {message.content}
                    </Typography>
                    {message.reactions > 0 && (
                      <Chip
                        label={`${message.reactions} reactions`}
                        size="small"
                        sx={{ mt: 1 }}
                      />
                    )}
                  </Box>
                }
              />
            </ListItem>
          ))}
        </List>
      </Paper>
    </Box>
  );
};

export default Messages;
