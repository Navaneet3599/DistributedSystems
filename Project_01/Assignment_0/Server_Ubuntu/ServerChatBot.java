import java.io.*;
import java.net.*;
import java.util.concurrent.*;

public class ServerChatBot
{

    public static final String RESET = "\033[0m";
    public static final String RED = "\033[0;31m";   
    public static final String GREEN = "\033[0;32m";
    public static final String BLUE = "\033[0;34m";
    private static Semaphore mutex = new Semaphore(1);
    private static boolean disconnectRequest = false;
    
    public static void sendMessageToTerminal(String message)
    {
    	
    }
    
    public static class serverHandler implements Runnable
    {
    	private Thread t;
    	private Socket clientSocket;
    	private PrintWriter sendMessage;
    	private BufferedReader readFromConsole;
    	
    	public serverHandler(Socket s)
    	{
    		try {
    			this.clientSocket = s;
    			this.readFromConsole = new BufferedReader(new InputStreamReader(System.in));
    			this.sendMessage = new PrintWriter(this.clientSocket.getOutputStream(), true);
			} catch (IOException e) {
				System.out.println(RED + "Server::Server encountered an error while creating a PrintWriter object..." + RESET);
			}
    	}
    	
    	public void run()
    	{
    		String message = "To exit chat please enter \"78Be1\"";
    		this.sendMessage.println(message);
    		while((message != null) && (!message.equals("78Be1")))
    		{
    			try {
					mutex.acquire();
					if(disconnectRequest == true)
					{
						System.out.println(RED + "Server::Client initiated a disconnect request..." + RESET);
						break;
					}
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					System.out.println(RED + "Server::Server(write) encountered an error while acquiring the mutex..." + RESET);
				} finally
    			{
					mutex.release();
    			}
    			
    			
    			try {
        			message = this.readFromConsole.readLine();
        			message = BLUE + "\033[A\rServer::" + message + RESET;
    				System.out.println(message);
    				this.sendMessage.println(message);
    			} catch (IOException e) {
    				System.out.println(RED + "Server::Server encountered an error while reading from console..." + RESET);
    			}	
    			
    			if((message != null) && (message.equals("78Be1")))
    			{
    				try {
    					mutex.acquire();
    					disconnectRequest = true;
    				} catch (InterruptedException e) {
    					// TODO Auto-generated catch block
    					e.printStackTrace();
    				} finally
    				{
    					mutex.release();
    				}
    	    		
    	    		System.out.println(RED + "Server::Server initiated a disconnect request..." + RESET);
    			}
    		}
    		
    		
    		
    		try {
				this.readFromConsole.close();
				this.sendMessage.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				System.out.println(RED + "Server::Server encountered an error while disconnection(writer)..." + RESET);
			}
    	}
    	
    	public void start()
    	{
    		if(t == null)
    		{
    			t = new Thread(this);
    			t.start();
    		}
    	}
    }
    
    public static class clientHandler implements Runnable
    {
    	private Thread t;
    	private Socket clientSocket;
    	private BufferedReader readFromClient;
    	public clientHandler(Socket s)
    	{
    		this.clientSocket = s;
    		try {
				this.readFromClient = new BufferedReader(new InputStreamReader(this.clientSocket.getInputStream()));
			} catch (IOException e) {
				System.out.println(RED + "Server::Server encountered an error while creating a buffered reader for client socket..." + RESET);
			}
    	}
    	
    	public void run()
    	{
    		String message = "";
    		while((message != null)&&(!message.equals("78Be1")))
    		{
    			try {
    				
    				try {
    					mutex.acquire();
    					if(disconnectRequest == true)
    					{
    						System.out.println(RED + "Server::Server initiated a disconnect request..." + RESET);
    						break;
    					}
    				} catch (InterruptedException e) {
    					// TODO Auto-generated catch block
    					System.out.println(RED + "Server::Server(write) encountered an error while acquiring the mutex..." + RESET);
    				} finally
        			{
    					mutex.release();
        			}
    				
    				
        			message = this.readFromClient.readLine();
    				System.out.println(message);
    				
    				try {
						if((message != null) && (message.equals("78Be1")))
						{
							mutex.acquire();
							disconnectRequest = true;
							System.out.println(RED + "Server::Client initiated a disconnect request..." + RESET);
							break;
						}
					} catch (InterruptedException e) {
						// TODO Auto-generated catch block
						System.out.println(RED + "Server::Server(write) encountered an error while acquiring the mutex..." + RESET);
					} finally
    				{
						mutex.release();
    				}
    				
    			} catch (IOException e) {
    				System.out.println(RED + "Server::Server encountered an error while reading from client socket..." + RESET);
    			}	
    		}

    		
    		try {
				this.readFromClient.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				System.out.println(RED + "Server::Server encountered an error while disconnection(reader)..." + RESET);
			}
    	}
    	
    	public void start()
    	{
    		if(t == null)
    		{
    			t = new Thread(this);
    			t.start();
    		}
    	}
    }
    
	public static void main(String[] args)
	{
		int port = 50280;

		try {
			ServerSocket serverSocket = new ServerSocket(port);
			System.out.println("Server waiting for clients to connect...");
			Socket clientSocket = serverSocket.accept();
			
			Thread serverThread  = new Thread(new serverHandler(clientSocket));
			Thread clientThread  = new Thread(new clientHandler(clientSocket));
			
			serverThread.start();
            clientThread.start();
            
            try {
				serverThread.join();
				clientThread.join();
			} catch (InterruptedException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} finally
            {
				serverSocket.close();
				clientSocket.close();
            }
            
			
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		
	}

}
